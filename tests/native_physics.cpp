#include "../client/motion.h"
#include <stdio.h>
#include <stdlib.h>
using namespace rp;
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    FILE *input=fopen(argv[1],"r");if(!input)return 2;
    char line[1024];int rows=0,grounded=0,airborne=0,crouched=0;float maximum=0;
    while(fgets(line,sizeof(line),input)){
        int mode,cmd,flags,before,after;Vec blast,feet,mins,maxs,actual;
        int fields=sscanf(line,"%d,%d,%d,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%d,%d",
            &mode,&cmd,&flags,&blast.x,&blast.y,&blast.z,&feet.x,&feet.y,&feet.z,&mins.x,&mins.y,&mins.z,
            &maxs.x,&maxs.y,&maxs.z,&actual.x,&actual.y,&actual.z,&before,&after);
        if(fields!=20||before<=after||actual.length()<1){fclose(input);return 1;}
        Vec calculated=selfImpulse(blast,feet,mins,maxs,(flags&1)!=0,{},(flags&512)!=0);
        float error=(calculated-actual).length();if(error>maximum)maximum=error;
        printf("case=%d command=%d delta=%.6f health=%d->%d grounded=%d hull=%.1f\n",mode,cmd,error,before,after,(flags&1)!=0,maxs.z-mins.z);
        if(error>.05f){fclose(input);return 1;}
        rows++;if(flags&1)grounded++;else airborne++;if(maxs.z-mins.z<70)crouched++;
    }
    fclose(input);
    printf("native impulse: rows=%d ground=%d air=%d crouch=%d max_error=%.6f units/s\n",rows,grounded,airborne,crouched,maximum);
    return rows>=2&&grounded>0&&airborne>0?0:1;
}
