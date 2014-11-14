/*****************************************************************************
 *
 * fetchlog.c - logfile fetcher: pick up last new messages of a logfile
 *
 * Copyright (c) 2002 .. 2010 Alexander Haderer (alexander.haderer@loescap.de)
 *
 *  Last Update:      $Author: afrika $
 *  Update Date:      $Date: 2010/07/01 16:39:25 $
 *  Source File:      $Source: /home/cvsroot/tools/fetchlog/fetchlog.c,v $
 *  CVS/RCS Revision: $Revision: 1.9 $
 *  Status:           $State: Exp $
 *
 *  CVS/RCS Log at end of file
 *
 * License:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************/


#include <stdio.h>	/* sprintf */
#include <stdlib.h>	/* atoi */
#include <ctype.h>	/* isalpha */
#include <string.h>	/* strcat, strcpy */
#include <fcntl.h>	/* open close */
#include <sys/types.h>	/* stat */
#include <sys/stat.h>	/* stat */
#include <sys/mman.h>	/* mmap, madvise */
#include <unistd.h>	/* access */
#include <errno.h>	/* errno */
#ifdef HAS_REGEX
#include <regex.h>	/* posix regex stuff */
#endif

/*************************************************
 * constants
 *************************************************/

#define MIN_FIRSTCOL	1	/* Min first col for fetching	*/
#define MAX_LASTCOL 	300 	/* Max last col for fetching	*/
#define MIN_COLS	20	/* Min no of cols to fetch	*/
#define MIN_FETCHLEN    50	/* Min length of fetched data */
#define MAX_FETCHLEN    20000	/* Max length of fetched data */

#define OK_MESSAGE	"OK: no messages"
#define ERR_MESSAGE	"ERROR: fetchlog: "

/* suffix for temp bookmarkfile:  mkstemp() template */
#define FETCH_FILE_SUFFIX "XXXXXX"  

/* conversion flags */
#define CONV_NONE	0
#define CONV_BRACKET	1
#define CONV_PERCENT	2
#define CONV_NEWLINE	4
#define CONV_OKMSG	8
#define CONV_SHELL	16
#define CONV_NAGIOS3	32

/* return/exit codes */
#define RET_OK		0	/* ok, no messages		ok */
#define RET_ERROR	1	/* internal error 		warn */
#define RET_NEWMSG	2	/* logfile has new messages	critical */
#define RET_PARAM	3	/* wrong parameter, print help */

/* version info */
#define FL_VERSION FETCHLOG_VERSION_NO

/*************************************************
 * typedefs
 *************************************************/
typedef struct {
    long last;		/* pos lastchar fetched plus 1 (=first new char) */
    time_t mtime;	/* mtime of logfile when this bookmark was valid */
    ino_t inode;	/* inode number of logfile */
} bookmark;

/*************************************************
 * prototypes
 *************************************************/
void usage( void );
int fetch_logfile( char *logfilename, char *bookmarkfile, int updbm_flag );
int read_bookmark( char *bmfile, bookmark *bm );
int write_bookmark( char *bmfile, bookmark *bm );
int copyline( int opos, char *obuf, char *ipt, int illen );
int check_farg( char *farg, int *conv );
void perr( char *msg1, char *msg2, int err );

/*************************************************
 * globals
 *************************************************/

/* fetching */
int firstcol_G = MIN_FIRSTCOL;
int lastcol_G = MAX_LASTCOL;
int fetchlen_G = MAX_FETCHLEN;
int conv_G = CONV_NONE;
#ifdef HAS_REGEX
regex_t *rx_G = NULL;
int numrx_G  = 0;

#define RXERRBUFLEN 1000
char rxerrbuf_G[RXERRBUFLEN];

#endif

/*************************************************
 * code...
 *************************************************/

/************************************************
 * perr( .. )
 * print error message to stdout with respect to fetchlen_G
 ************************************************
 */
void perr( char *msg1, char *msg2, int err ) {
    char *msg = NULL;
    int len = 0;
    int r;

    if( !msg1 ) return;

    len = sizeof( ERR_MESSAGE ) + strlen( msg1 ) + 1;	/* 1 == '\n' */
    if( msg2 ) len += strlen( msg2 ) + 2;		/* 2 == ': ' */
    if( err ) len += strlen( strerror( err ) ) + 2;	/* 2 == ': ' */

    if( (msg=malloc( len ) ) == NULL ) { exit( RET_ERROR ); }

    strcpy( msg, ERR_MESSAGE );
    strcat( msg, msg1 );
    if( msg2 ) {
	strcat( msg, ": " );
	strcat( msg, msg2 );
    }
    if( err ) {
	strcat( msg, ": " );
	strcat( msg, strerror( err ) );
    }
    if( len-1 <= fetchlen_G ) {
	strcat( msg, "\n" );
    }else{
	msg[fetchlen_G-1] = '\n';
	msg[fetchlen_G-2] = '~';
	len = fetchlen_G + 1;
    }
    r = write( STDOUT_FILENO, msg, len-1 );
    free( msg );
}

/************************************************
 * read_bookmark( char *bmfile, bookmark *bm );
 * return: 0: ok    1: error
 ************************************************
 */
int read_bookmark( char *bmfile, bookmark *bm ) {
    int fd = -1;
    struct stat sb;

    fd = open( bmfile, O_RDONLY );
    if( fd == -1 ) {
	if( errno == ENOENT ) {
	    /* no bookmarkfile --> acts like infinite old bookmarkfile */
	    bm->last = -1;
	    bm->mtime = 0;
	    bm->inode = 0;
	    return 0;
	}else{
	    perr( "open", bmfile, errno );
	    return 1;
	}
    }
    if( fstat( fd, &sb ) == -1 ) {
	perr( "stat", bmfile, errno );
	close( fd );
	return 1;
    }
    if( (int)sb.st_size != sizeof(bookmark) || 
	((sb.st_mode & S_IFMT) != S_IFREG) ) 
    {
	perr( "no file/wrong size", bmfile, 0);
	close( fd );
	return 1;
    }
    if( read( fd, bm, sizeof(bookmark)) != sizeof( bookmark ) ) {
	perr( "file to short", bmfile, 0 );
	close( fd );
	return 1;
    }
    close( fd );
    
    return 0;  /* ok */

}   /* read_bookmark() */


/************************************************
 * write_bookmark( char *bmfile, bookmark *bm );
 * return: 0: ok    1: error
 ************************************************
 */
int write_bookmark( char *bmfile, bookmark *bm ) {

    char *nbmfile = NULL; /* new bmfile (a tmp file to be renamed to bmfile) */
    int nbmfd = -1;

    nbmfile = (char*)malloc( strlen(bmfile) + sizeof(FETCH_FILE_SUFFIX) );
    if( nbmfile == NULL ) {
	perr("malloc", NULL, errno );
	return 1;
    }
    strcpy( nbmfile, bmfile );
    strcat( nbmfile, FETCH_FILE_SUFFIX );
    nbmfd = mkstemp( nbmfile );
    if( nbmfd == -1 ) {
	perr( "mkstemp", nbmfile, errno );
	free( nbmfile );
	return 1;
    }
    if( write( nbmfd, bm, sizeof( bookmark ) ) != sizeof( bookmark )  ) {
	perr( "write", nbmfile, errno );
	close( nbmfd );
	unlink( nbmfile );
	free( nbmfile );
	return 1;
    }
    close( nbmfd );
    if( rename( nbmfile, bmfile ) == -1 ) { 
	perr( "rename tmpfile to", bmfile, errno );
	unlink( nbmfile );
	free( nbmfile );
	return 1;
    }
    free( nbmfile );
    return 0;	/* ok */
    
}   /* write_bookmark() */



/************************************************
 * fetch_logfile( char *logfilename, char *bookmarkfile, int updbm_flag ) 
 ************************************************
 */
int fetch_logfile( char *logfile, char *bmfile, int updbm_flag ) {

    bookmark obm, nbm;		/* old, new bookmark */

    char *ibuf = NULL;		/* input buf (--> the logfile) */
    size_t ilen = 0;
    char *ipt  = NULL;
    char *bmpt = NULL;		/* points to first new char if bm exists */

    char *obuf = NULL;		/* output buf (filled backwards) */
    int opos;			/* first used char pos in obuf */

    				/* for CONV_NAGIOS3 */
    int lastlinepos = 0;	/*   pos of beginning of lastline in obuf */
    int lastlinelen = 0;	/*   len of lastline, excl. trailing '\' 'n' */

    int r;			/* write's returncode */

    int i;
    int fd = -1;
    struct stat sb;
    char *lgfile = NULL;
    int llen = 0;
    int done = 0;

    if( read_bookmark( bmfile, &obm ) ) return RET_ERROR;

    if( (fd=open( logfile, O_RDONLY ))  == -1 ) {
	perr( "open", logfile, errno );
	return RET_ERROR;
    }
    if( fstat( fd, &sb ) == -1 ) {
	perr( "stat", logfile, errno );
	close( fd );
	return RET_ERROR;
    }

    nbm.last  = (size_t) sb.st_size;
    nbm.mtime = sb.st_mtime;
    nbm.inode = sb.st_ino;

    /* something changed meanwhile ? */
    if( obm.mtime==nbm.mtime && obm.inode==nbm.inode && obm.last==nbm.last ) {
	if( conv_G & CONV_OKMSG ) 
	    r = write( STDOUT_FILENO, OK_MESSAGE "\n", sizeof( OK_MESSAGE ) );
	close( fd );
	return RET_OK;
    }

    /*****************/

    obuf = malloc( fetchlen_G+1 );
    if( obuf == NULL ) {
	perr( "malloc", NULL,  errno );
	close( fd );
	return RET_ERROR;
    }
    opos = fetchlen_G;
    *(obuf + opos) = '\0';	/* dummy: opos -> first used char in obuf */
    if( conv_G & CONV_NEWLINE ) {
	/* when using CONV_NEWLINE the obuf is filled up like this:
	     1. init: write '\\'  'n'  '\n' to the end (3 chars)
	     2. fill (copyline() ) by first prepend line contents and 
	     	then prepend '\\' 'n'
	     result: An additional '\\'  'n'  at the beginning 
	   else
	     1. fill (copyline() ) by first prepend newline and then prepend 
	     	line contents 
	*/
	*(obuf + --opos) = '\n';
	*(obuf + --opos) = 'n';
	*(obuf + --opos) = '\\';
    }

    lgfile = (char*)malloc( strlen(logfile) + sizeof(".X") );
    if( lgfile == NULL ) {
	free( obuf );
	perr( "malloc", NULL, errno );
	close( fd );
	return RET_ERROR;
    }

    /* read in all logfiles and backward fill obuf upto fetchlen_G chars */

    for( i=-1; i<10; i++ ) {
	/* i==-1: logfile without suffix, else i==logfile suffix */
	if( i==-1 ) {
	    /* lgfile is already open and sb contains the stat */
	    strcpy( lgfile, logfile );
	}else{
	    sprintf( lgfile, "%s.%1d", logfile, i );
	
	    if( (fd=open( lgfile, O_RDONLY )) == -1 ) {
		if( errno==ENOENT && i==0 ) { 
		    continue;           /* some logrotator start with .1 */
		}else if( errno==ENOENT && i>0 ) {  
		    break; 
		}else{
		    perr( "open", lgfile, errno );
		    free( obuf ); free( lgfile );
		    return RET_ERROR;
		}
	    }
	    if( fstat( fd, &sb ) == -1 ) {
		perr( "stat", lgfile, errno );
		free( obuf ); free( lgfile ); close( fd );
		return RET_ERROR;
	    }
	}
	
	ilen = (size_t) sb.st_size;

	if( ilen == 0 ) {
	    close( fd );
	    if( obm.inode == sb.st_ino ) break;
	    continue;
	}

	ibuf = mmap( NULL, ilen, PROT_READ, MAP_SHARED, fd, (off_t)0 );
	if( ibuf == MAP_FAILED ) {
	    perr( "mmap", lgfile, errno );
	    free( obuf ); free( lgfile ); close( fd );
	    return RET_ERROR;
	}
	
#ifdef HAS_MADVISE
	if( madvise( ibuf, ilen, MADV_RANDOM ) ) {
	    perr( "madvise", NULL, errno );
	    free( obuf ); free( lgfile ); close( fd );
	    munmap( ibuf, ilen );
	    return RET_ERROR;
	}
#endif
	
	/* check for old bookmark */
	bmpt = NULL;
	if( obm.inode == sb.st_ino ) {
	    bmpt = ibuf+obm.last;
	}

	/* scan backwards for lines but the first */
	done = 0;
	for( llen=1,ipt=ibuf+ilen-2; ipt>=ibuf; llen++,ipt-- ) {
	    if( *ipt=='\n' ) {
		if( ipt+1<bmpt ) { done=1; break; }
		opos = copyline( opos, obuf, ipt+1, llen );
		if( opos==0 ) { done=1; break; }
		llen = 0;
	    }
	}
	/* copy first line ? */
	if( ipt+1==ibuf && done==0 ) {	
	    if( ipt+1<bmpt ) { done=1; }
	    else{ opos = copyline( opos, obuf, ipt+1, llen ); }
	    if( opos==0 ) { done=1; }
	}

	munmap( ibuf, ilen );
	close( fd );
	if( done ) break;
	if( bmpt ) break;	/* processed a bookmarked file? --> finito */
    }

    if( updbm_flag ) {
	if( write_bookmark( bmfile, &nbm ) ) return RET_ERROR;
    }

    /* if in Nagios3 mode: prepend short message (the last line fetched) */
    if( conv_G & CONV_NAGIOS3 ) {
	/* Nagios2 --> Nagios3 changed accepted format for plugin output.
	 * Nagios2 accepted a line containing one or more '\'+'n' as a single 
	 * line. Nagios3 now supports multiline output, and as a result, lines
	 * containing '\'+'n' are now handled as multiline messages (as well as
	 * messages having '\n'). The new multiline format is:
	 *    SHORT_MESSAGE | OPTIONAL_PERFORMANCE_DATA
	 *    LONG_MESSAGE_LINE_1
	 *    LONG_MESSAGE_LINE_2
	 *    	    ...
	 *    LONG_MESSAGE_LINE_N
	 *
	 * In Nagios3 mode fetchlog copies LONG_MESSAGE_LINE_N as SHORT_MESSAGE
	 * and leaves OPTIONAL_PERFORMANCE_DATA empty
	 */

	int oidx = fetchlen_G - 4; 	/* in obuf: last char, last line */
	lastlinepos = oidx + 1;		/* fallback value: empty line at end */
	lastlinelen = 0;		/* fallback value: empty line at end */

	/* determine lastlinepos and lastlinelen */
	while( oidx > 0 ) {
	    if( *(obuf + oidx) == 'n' && *(obuf + oidx -1) == '\\' ) {
		lastlinepos = oidx + 1;
		lastlinelen = fetchlen_G - lastlinepos - 3;  /* 3: \ n \n */
		break;
	    }
	    oidx--;
	}

	/* case: obuf has enough room for SHORT_MESSAGE */
	if( lastlinelen + 1 <= opos ) {		/* +1 = '|' */
	    *(obuf + --opos) = '|';
	    memmove( obuf+opos-lastlinelen, obuf+lastlinepos, lastlinelen );
	    opos -= lastlinelen;

	/* case: obuf too small: SHORT_MESSAGE and fetched messages overlap,
	 *       but not LONG_MESSAGE_LINE_N */
	}else if( lastlinelen + 6 <= lastlinepos ) { /* +6 = '|\n...' */
	    memmove( obuf, obuf+lastlinepos, lastlinelen );
	    *(obuf + lastlinelen + 0 ) = '|';
	    *(obuf + lastlinelen + 1 ) = '\\';
	    *(obuf + lastlinelen + 2 ) = 'n';
	    *(obuf + lastlinelen + 3 ) = '.';
	    *(obuf + lastlinelen + 4 ) = '.';
	    if( *(obuf + lastlinelen + 5 ) != '\\' ) {
	       	*(obuf + lastlinelen + 5 ) = '.';
	    }
	    opos = 0;

	/* case: obuf too small: SHORT_MESSAGE and fetched messages overlap,
	 *       including LONG_MESSAGE_LINE_N */
	}else{
	    memmove( obuf+lastlinepos+2, obuf+lastlinepos, lastlinelen );
	    opos = lastlinepos + 2;
	}
    }

    /* only return a message if there is something to print */
    if( ((conv_G & CONV_NEWLINE)==0 && fetchlen_G-opos==0 ) || 
        ((conv_G & CONV_NEWLINE)!=0 && 
	    ( ((conv_G & CONV_NAGIOS3)== 0 && fetchlen_G-opos==3 ) ||
	      ((conv_G & CONV_NAGIOS3)!= 0 && fetchlen_G-opos==4 )    )  ) ) {

	if( conv_G & CONV_OKMSG ) {
	    r = write( STDOUT_FILENO, OK_MESSAGE "\n", sizeof( OK_MESSAGE ) );
	}
	i = RET_OK;
    }else{
	r = write( STDOUT_FILENO, obuf+opos,fetchlen_G-opos);
	i = RET_NEWMSG;
    }

    free( obuf ); free( lgfile );
    return i;

}   /* fetch_logfile() */


/************************************************
 * copyline( int opos, char *obuf, char *ipt, int illen );
 * trim new line (ipt, illen), copy it to obuf, convert it, return new opos
 ************************************************
 */
int copyline( int opos, char *obuf, char *ipt, int illen ) {

    char *p = NULL;
    int len = 0;
    int i;
#ifdef HAS_REGEX
    int rxerr = 0;
    char rxbuf[MAX_LASTCOL+1];	/* +1 for extra '\0' */

    if( numrx_G > 0 ) {
	/* we have to copy the line to another buffer because regexec()
	   wants a c-string, and we only have a \n terminated line in ipt.
	   Some platforms (e.g. FreeBSD) have a non-portable extension in
	   regcomp() which allows to regex non-null terminated string slices,
	   but to be portable...
	 */
	if( illen <= firstcol_G ) {
	    rxbuf[0] = '\0';
	}else{
	    if( illen > lastcol_G ) len = lastcol_G;
	    else 		    len = illen - 1;
	    len -= firstcol_G - 1;
	    memcpy( rxbuf, ipt+firstcol_G-1, len );
	    if( illen-1 > lastcol_G ) rxbuf[len-1] = '~';
	    rxbuf[len] = '\0';
	}

	for( i=0; i<numrx_G; i++ ) {
	    rxerr = regexec( &(rx_G[i]), rxbuf, (size_t)0, NULL, 0);
	    if( rxerr == 0 )  break;	/* match */
	    /* else: no match (rxerr==REG_NOMATCH) or an error (ignored) */ 
	}
	if( rxerr != 0 ) return opos;
    }
#endif

    if( conv_G & CONV_NEWLINE ) {
	/* fill obuf: 
	   prepend  concat( '\\' + 'n' + iline )   (newline sequence first) */
	if( opos > 2 ) {
	    if( illen <= firstcol_G ) {
		*(obuf+opos-1) = 'n'; *(obuf+opos-2) = '\\'; 
		opos -= 2;
	    }else{
		if( illen > lastcol_G ) len = lastcol_G;
		else 		        len = illen - 1;
		len -= firstcol_G - 1;
		if( len+2 > opos ) {
		    memcpy( obuf+2, ipt+firstcol_G-1+len+2-opos, opos-2 );
		    len = opos - 2;
		}else{
		    memcpy( obuf+opos-len, ipt+firstcol_G-1, len );
		}
		if( illen-1 > lastcol_G ) *(obuf+opos-1) = '~';
		opos -= len+2;
		*(obuf+opos+1) = 'n'; 
		*(obuf+opos+0) = '\\'; 
	    }
	}else{
	    opos = 0;
	}
	if( opos==0 ) {
	    p = obuf;
	    *p++='\\'; *p++='n'; *p++='.'; *p++='.'; *p++='.';
	    if( obuf[5]=='n' ) { *p++='.'; }
	}
    }else{
	/* without newline conversion */

	/* fill obuf: 
	   prepend concat( iline + '\n' )  (newline char last) */
	if( opos > 1 ) {
	    if( illen <= firstcol_G ) {
		*(obuf+opos-1) = '\n'; 
		opos -= 1;
	    }else{
		if( illen > lastcol_G ) len = lastcol_G;
		else 		        len = illen - 1;
		len -= firstcol_G - 1;
		if( len+1 > opos ) {
		    memcpy( obuf, ipt+firstcol_G-1+len+1-opos, opos-1 );
		    len = opos - 1;
		}else{
		    memcpy( obuf+opos-len-1, ipt+firstcol_G-1, len );
		}
		*(obuf+opos-1) = '\n'; 
		if( illen-1 > lastcol_G ) *(obuf+opos-2) = '~';
		opos -= len+1;
	    }
	}else{
	    opos = 0;
	}
	if( opos==0 ) {
	    p = obuf;
	    *p++='.'; *p++='.'; *p++='.';
	}
    }

    p = obuf+opos;
    if( conv_G & CONV_NEWLINE )  p += 2;	/* +2: skip '\\' 'n' */

    if( conv_G & CONV_PERCENT ) {
	for( i=0; i<len; i++ ) {
	    if( *(p+i) == '%' ) *(p+i) = 'p';
	}
    }

    if( conv_G & CONV_BRACKET ) {
	for( i=0; i<len; i++ ) {
	    if(      *(p+i) == '<' ) *(p+i) = '(';
	    else if( *(p+i) == '>' ) *(p+i) = ')';
	}
    }

    if( conv_G & CONV_SHELL ) {
	for( i=0; i<len; i++ ) {
	    if(      *(p+i) == '$' )  *(p+i) = '_';
	    else if( *(p+i) == '\'' ) *(p+i) = '_';
	    else if( *(p+i) == '\"' ) *(p+i) = '_';
	    else if( *(p+i) == '`' )  *(p+i) = '_';
	    else if( *(p+i) == '^' )  *(p+i) = '_';
	    else if( *(p+i) == '\\' ) *(p+i) = '/';
	    else if( *(p+i) == '|' )  *(p+i) = '_';
	}
    }

    return opos;

}   /* copyline() */


/************************************************
 * check_farg( char *farg ) 
 * check if -f arg is in proper format for sccanf(): nnn:nnn:nnnn:XXXXX
 ************************************************
 */
int check_farg( char *farg, int *conv ) {
    char *pt = farg;
    int numdig = 0;
    int numc = 0;

    *conv = 0;

    for( numdig=0 ; *pt; pt++ ) {
	if( isdigit( (int) *pt ) ) numdig++;
	else if( *pt==':' ) break;
	else return 1;
    }
    if( numdig <1 || numdig > 3 ) return 1;

    for( pt++,numdig=0 ; *pt; pt++ ) {
	if( isdigit( (int) *pt ) ) numdig++;
	else if( *pt==':' ) break;
	else return 1;
    }
    if( numdig <1 || numdig > 3 ) return 1;

    for( pt++,numdig=0 ; *pt; pt++ ) {
	if( isdigit( (int) *pt ) ) numdig++;
	else if( *pt==':' ) break;
	else return 1;
    }
    if( numdig <1 || numdig > 5 ) return 1;

    for( pt++,numc=0 ; *pt; pt++ ) {
	if     ( *pt=='b' ) { *conv |= CONV_BRACKET; numc++; }
	else if( *pt=='p' ) { *conv |= CONV_PERCENT; numc++; }
	else if( *pt=='n' ) { *conv |= CONV_NEWLINE; numc++; }
	else if( *pt=='o' ) { *conv |= CONV_OKMSG;   numc++; }
	else if( *pt=='s' ) { *conv |= CONV_SHELL;   numc++; }
	else if( *pt=='3' ) { *conv |= CONV_NAGIOS3; numc++; }
	else return 1;
    }
    if( numc > 6 ) return 1;
    return 0;	/* ok */

}   /* check_farg() */

/*************************************************/
void usage( void ) {

    printf(
	"fetchlog - fetch the last new log messages - version %s\n\n"
	"   usage 1: fetchlog -f firstcol:lastcol:len:conv logfile bmfile [pattern ...]\n"
	"   usage 2: fetchlog -F firstcol:lastcol:len:conv logfile bmfile [pattern ...]\n"
	"   usage 3: fetchlog [ -V | -h ] \n\n"

	"1: Read all messages of <logfile> [matching any of regex <pattern>] which are\n"
	"newer than the bookmark in <bmfile>. Print at most <len> bytes from column\n"
	"<firstcol> upto column <lastcol> to stdout. Adds '...' characters when skip-\n"
	"ping old lines or '~' when cutting long lines. <conv> sets output conversion:\n"
	"      'b': convert < and > to ( and ) for safe HTML output\n"
	"      'p': convert %% to p for safe printf output\n"
	"      's': convert $'\"`^\\| to _____/_ for safe shell parameter input\n"
	"      'n': convert newline to \\n for single line output\n"
	"      'o': show '%s' message if no new messages\n"
	"      '3': output in format compatible with Nagios3 plugins (enables 'no')\n"
	"  0  <  <firstcol>  <  <lastcol>  <  %d\n"
	"  <lastcol> - <firstcol> > %d \n"
	"  <len> valid range %d..%d\n"
	"  <conv> is zero or more of 'bpsno3' \n"
	"  <logfile> absolute path to logfile\n"
	"  <bmfile> absolute path to bookmarkfile\n"
	"2: like 1 and update <bmfile> to remember fetched messages as 'read'\n"
	"3: print version (-V) or print this help message (-h) \n"
	, FL_VERSION, 
	OK_MESSAGE,MAX_LASTCOL+1,MIN_COLS-1,MIN_FETCHLEN, MAX_FETCHLEN
	);

} /* usage() */

/*************************************************/

int main(int argc, char **argv)
{
    int  ret=0;
    int  i;
    int  rxerr;

    /* check args */
    if (argc == 2) {
	if( argv[1][0] == '-' && argv[1][1] == 'V' ) {
	    printf( "fetchlog version %s \n", FL_VERSION );
	    exit( RET_PARAM );
	}
    }else if (argc >= 5) {
	if( argv[1][0] == '-' && 
	    argv[3][0] == '/' && isalpha((int)argv[3][1]) &&
	    argv[4][0] == '/' && isalpha((int)argv[4][1]) ) {
	    if( argv[1][1] == 'f' || argv[1][1] == 'F' ) {
		if( check_farg( argv[2], &conv_G ) ) {
		    perr( "invalid parameter", "firstcol, lastcol, len or conv", 0 );
		    exit( RET_PARAM );
		}
		ret = sscanf( argv[2], "%3d:%3d:%5d", 
			      &firstcol_G, &lastcol_G, &fetchlen_G );
		if( ret != 3 ) {
		    perr( "invalid parameter", NULL, 0 );
		    exit( RET_PARAM );
		}
		if( conv_G & CONV_NAGIOS3 ) {
		    /* nagios3: auto enable conversion 'okmsg' and 'newline' */
		    conv_G |= (CONV_NEWLINE | CONV_OKMSG);
		}
		if( firstcol_G >= MIN_FIRSTCOL &&
		    lastcol_G >= firstcol_G + MIN_COLS - 1 &&
		    lastcol_G <= MAX_LASTCOL &&
		    fetchlen_G >= MIN_FETCHLEN &&
		    fetchlen_G <= MAX_FETCHLEN ) {
		    
		    /* prepare regex, if any */
		    if( argc > 5 ) {
#ifdef HAS_REGEX
			numrx_G = argc - 5;
			rx_G = (regex_t*)malloc(numrx_G*sizeof(regex_t));
			if( rx_G == NULL ) {
			    perr("malloc", NULL, errno );
			    exit( RET_ERROR );
			}
			for( i=5; i<argc; i++ ) {
			    rxerr = regcomp( &(rx_G[i-5]), argv[i], 
					     REG_EXTENDED | REG_NOSUB );
			    if( rxerr ) {
				regerror( rxerr, &(rx_G[i-5]), 
					  rxerrbuf_G, RXERRBUFLEN );
				perr("regex", rxerrbuf_G, 0 );
				exit( RET_ERROR );
			    }
			}
#else
			i = 0; rxerr = 0;
			perr("regex", "not supported on this platform", 0 );
			exit( RET_ERROR );
#endif
		    }
		    if( argv[1][1] == 'f' ) {
			exit( fetch_logfile( argv[3], argv[4], 0 ) );
		    }else{
			exit( fetch_logfile( argv[3], argv[4], 1 ) );
		    }
		}else{
		    perr( "out of range: firstcol, lastcol or len", NULL, 0 );
		    exit( RET_PARAM );
		}
	    }
	}
    }
    usage();
    return RET_PARAM;

}   /* main() */

/*
 * CVS/RCS Log:
 * $Log: fetchlog.c,v $
 * Revision 1.9  2010/07/01 16:39:25  afrika
 * - bugfix: wrong exitcode when fetching with regex and logfile has changed
 * - new: test-all and make file target for this
 *
 * Revision 1.8  2010/06/18 18:17:47  afrika
 * - new option '3' for nagios3 compatible (multiline) output
 * - Added sample configs for Nagios 2 + 3
 * - Updated README.Nagios to Nagios 2 + 3
 * - Scanning for rotated logfiles now silently skips over to .1 if
 *   .0 is missing because some rotation tools start indexing with .1
 * - fixed: compiler warnings on Debian Linux
 *
 * Revision 1.7  2008/11/21 19:58:51  afrika
 * - update docs
 * - now '|' is shell critical character also
 * - added Greg Baker's files for building a .depot on hpux
 *
 * Revision 1.6  2004/03/26 19:46:03  afrika
 * added regex pattern matching
 *
 * Revision 1.5  2003/11/19 15:24:19  afrika
 * only return "new message" if there is at least one char to put out
 *
 * Revision 1.4  2003/11/18 18:44:23  afrika
 * - removed compile option pre 0.93 exit codes
 * - no longer use copy of last line fetched to find bookmark location
 * - use inode to identify a (rotated) logfile instead
 * - handling of empty (and rotated) logfiles now correct
 *
 * Revision 1.3  2002/12/17 18:40:05  afrika
 * exit code now nagios compatible
 *
 * Revision 1.2  2002/12/17 18:04:48  afrika
 * - inserted CVS tags
 * - change docs: Netsaint --> Nagios(TM)
 *
 *
 */
 
