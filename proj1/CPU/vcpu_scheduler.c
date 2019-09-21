#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libvirt/libvirt.h>

#define MAX_DOMAINS 512

void main(int argc, char **argv) {
    if(argc != 2){
        fprintf(stdout, "Usage: ./cpu refresh_interval\n");
        return;
    }
    int refreshInterval = atoi(argv[1]);
    if(refreshInterval < 0){
        fprintf(stdout, "refresh_interval should be an integer bigger than zero\n");
        return;
    }
    const char *hostName = "qemu:///system";
    int domainIds[MAX_DOMAINS];

    while (1) {
        virConnectPtr vcp = virConnectOpen(hostName);
        if (!vcp){
            fprintf(stderr, "Unable to connect: virConnectOpen()\n");
            continue;
        }

        int numDomains = virConnectListDomains(vcp, domainIds, MAX_DOMAINS);
        if (numDomains == -1) {
            fprintf(stderr, "Unable to get domains: virConnectListDomains()\n");
            continue;
        }

        fprintf(stderr, "Number of domains: %d\n", numDomains);


        // VIR_CPU_MAPLEN(CHANNEL_CMDS_MAX_CPUS)

        for (int i = 0; i < numDomains; i++) {
            virDomainPtr vdp = virDomainLookupByID(vcp, domainIds[i]);
            if (!vdp) {
                fprintf(stderr, "Unable to lookup domain: virDomainLookupByID()\n");
                continue;
            }

            virNodeInfo nodeinfo;
            virDomainInfo dominfo;
            int nhostcpus;

            if (virNodeGetInfo(vcp, &nodeinfo) < 0)
                return;

            nhostcpus = VIR_NODEINFO_MAXCPUS(nodeinfo);

            if (virDomainGetInfo(vdp, &dominfo) != 0)
                return;

            virVcpuInfoPtr cpuinfo = malloc(sizeof(virVcpuInfo)*dominfo.nrVirtCpu);
            int cpumaplen = VIR_CPU_MAPLEN(nhostcpus);
            unsigned char* cpumaps = malloc(dominfo.nrVirtCpu * cpumaplen);

            int ncpus;
            if ((ncpus = virDomainGetVcpus(vdp,
                                            cpuinfo, dominfo.nrVirtCpu,
                                            cpumaps, cpumaplen)) < 0)
                return;
            
            for(int j = 0; j < cpumaplen; j++){
                fprintf(stderr, "%d cpumap: %d\n", j, (int)cpumaps[j]);
            }



            int numParams = virDomainGetCPUStats(vdp, NULL, 0, -1, 1, 0); // nparams
            virTypedParameter* params = calloc(numParams, sizeof(virTypedParameter));
            virDomainGetCPUStats(vdp, params, numParams, -1, 1, 0); // total stats.
            for (int j = 0; j < numParams; j++) {
                //print out params[j].something
                fprintf(stderr, "the params[%d].field is %s, value is: %llu\n", j, params[j].field, params[j].value.l);
                // switch(params[j].type){
                //     case 1:

                // }
            }
            free(params);
        }
        sleep(refreshInterval);
    } 
}