#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

bool up_load_config(UpConfig *out) {
    *out=(UpConfig){.enabled=true,.dry_run=false,.poll_ms=500,.psc_module_id=0x7b,
        .timing={.settle_ms=10000,.stable_ms=2000,.cooldown_ms=30000}};
    FILE *file=fopen("sdmc:/config/usb-pd-ufp/config.ini","r");
    if(!file) {
        if(errno!=ENOENT) {out->enabled=false; return false;}
        return true;
    }
    bool good=true;
    char line[192];
    while(fgets(line,sizeof(line),file)) {
        char *p=line;
        while(isspace((unsigned char)*p)) ++p;
        if(!*p || *p=='#' || *p==';' || *p=='[') continue;
        char *equals=strchr(p,'=');
        if(!equals) {good=false; break;}
        *equals++=0;
        char *endkey=p+strlen(p);
        while(endkey>p && isspace((unsigned char)endkey[-1])) *--endkey=0;
        char *end;
        errno=0;
        unsigned long value=strtoul(equals,&end,0);
        if(end==equals || errno || value>UINT32_MAX) {good=false; break;}
        while(isspace((unsigned char)*end)) ++end;
        if(*end && *end!='#' && *end!=';') {good=false; break;}
        if(!strcmp(p,"enabled")) { if(value>1) {good=false;break;} out->enabled=value; }
        else if(!strcmp(p,"dry_run")) { if(value>1) {good=false;break;} out->dry_run=value; }
        else if(!strcmp(p,"poll_ms")) out->poll_ms=value;
        else if(!strcmp(p,"settle_ms")) out->timing.settle_ms=value;
        else if(!strcmp(p,"stable_ms")) out->timing.stable_ms=value;
        else if(!strcmp(p,"cooldown_ms")) out->timing.cooldown_ms=value;
        else if(!strcmp(p,"psc_module_id")) out->psc_module_id=value;
        else { good=false; break; }
    }
    if(ferror(file)) good=false;
    fclose(file);
    if(out->poll_ms<100 || out->poll_ms>5000 || out->timing.settle_ms<5000 ||
       out->timing.settle_ms>60000 || out->timing.stable_ms<1000 || out->timing.stable_ms>10000 ||
       out->timing.cooldown_ms<30000 || out->timing.cooldown_ms>300000 ||
       out->psc_module_id<0x70 || out->psc_module_id>0x7e) good=false;
    if(!good) out->enabled=false;
    return good;
}
