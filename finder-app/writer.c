#include <stdio.h>
#include <syslog.h>

int main(int argc , char *argv[]) {


       if (argc < 3) 
       {
        syslog(LOG_ERR, "Usage: %s <writefile> <writestr>\n", argv[0]);
        return 1;
      }

    const char *writefile = argv[1];
    const char *writestr = argv[2];
    
    setlogmask(LOG_UPTO(LOG_DEBUG));
    openlog("writer", LOG_PID, LOG_USER);
    
     FILE *fp =fopen(writefile, "w");
     if (fp == NULL)
      {
        syslog(LOG_ERR, "Failed to open file %s for writing", writefile);
        closelog();
        return 1 ;
      }
      
      if (fprintf(fp, "%s", writestr) < 0) 
      {
        syslog(LOG_ERR, "Failed to write to file %s", writefile);
        fclose(fp);
        closelog();
        return 1; 
      }
    
    syslog(LOG_DEBUG, "Writing '%s' to '%s'", writestr, writefile);
    fclose(fp);
    closelog();
    return 0;
}
