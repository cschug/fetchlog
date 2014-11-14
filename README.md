fetchlog
========

The fetchlog utility displays the last new messages of a logfile. It is similar like tail but offers some extra functionality for output formatting. To show only the new messages appeared since the last call fetchlog uses a bookmark to remember which messages have been fetched. 
The project is originated from a linux tool - fetchlog(http://linux.die.net/man/1/fetchlog). 


Usage
========
./fetchlog -F 1:999:256000000:o /home/rtmonitor/biz.log /home/rtmonitor/bmfile

see README