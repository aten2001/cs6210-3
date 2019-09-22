#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <libvirt/libvirt.h>

#define MAX_DOMAINS 512

void printNodeInfo(virNodeInfo info){
	printf(" n : model == %s\n", info.model);
//	printf(" n : memory size  == %lu kb\n", info.memory);
	printf(" n : # of active CPUs == %d\n", info.cpus);
	printf(" n : expected CPU frequency  == %d mHz\n\n", info.mhz);

}

int getPercentageForPcpus(virDomainPtr* domains, double* usagePercentageArrayPcpu, int numDomains, int numPcpus){
    static unsigned long long currentTime = 0;
    static unsigned long long domainCpuTime[MAX_DOMAINS] = {0};

    if(currentTime == 0){
        // need to initialize first;
        struct timespec curTime;
        clock_gettime(CLOCK_MONOTONIC, &curTime);
        currentTime = curTime.tv_sec * 1000000000UL + curTime.tv_nsec;

        for(int i = 0; i < numDomains; i++){
            virVcpuInfo vcpuInfo;
            virDomainGetVcpus(domains[i], &vcpuInfo, 1, NULL, 0);
            domainCpuTime[i] = vcpuInfo.cpuTime;
        }
    }else{
        memset(usagePercentageArrayPcpu, 0, numPcpus * sizeof(double));

        struct timespec curTime;
        clock_gettime(CLOCK_MONOTONIC, &curTime);
        unsigned long long newTime = curTime.tv_sec * 1000000000UL + curTime.tv_nsec;

        for(int i = 0; i < numDomains; i++){
            virVcpuInfo vcpuInfo;
            virDomainGetVcpus(domains[i], &vcpuInfo, 1, NULL, 0);
            usagePercentageArrayPcpu[vcpuInfo.cpu] += (100.0 * (vcpuInfo.cpuTime - domainCpuTime[i])) / (newTime - currentTime);
            domainCpuTime[i] = vcpuInfo.cpuTime;
        }
        currentTime = newTime;
    }
}

void needScheduling(double* usagePercentageArrayPcpu, int numPcpu, int* maxIdx, int* minIdx){
    *maxIdx = 0, *minIdx = 0;

    for(int i = 0; i < numPcpu; i++){
        if(usagePercentageArrayPcpu[i] > usagePercentageArrayPcpu[*maxIdx]){
            *maxIdx = i;
        }
        if(usagePercentageArrayPcpu[i] < usagePercentageArrayPcpu[*minIdx]){
            *minIdx = i;
        }
    }
    // load balancedly distributed, no need for scheduling
    if(usagePercentageArrayPcpu[*maxIdx] - usagePercentageArrayPcpu[*minIdx] < 10.0){
        printf("load balancedly distributed, no need for scheduling at this time\n");
        *maxIdx = -1, *minIdx = -1;
    }
}

int main(int argc, char **argv) {
    if(argc != 2){
        fprintf(stdout, "Usage: ./cpu refresh_interval\n");
        return -1;
    }
    int refreshInterval = atoi(argv[1]);
    if(refreshInterval < 0){
        fprintf(stdout, "refresh_interval should be an integer bigger than zero\n");
        return -1;
    }
    const char *hostName = "qemu:///system";
    int domainIds[MAX_DOMAINS];

    virConnectPtr conn = virConnectOpen(hostName);
    if (!conn){
        fprintf(stderr, "Unable to connect: virConnectOpen()\n");
        return -1;
    }

    virNodeInfo nodeinfo;
    if (virNodeGetInfo(conn, &nodeinfo) < 0){
        fprintf(stderr, "Unable to get node info: virNodeGetInfo()\n");
        return -1;
    }
    
    int numPcpus = (int)nodeinfo.cpus;
    printNodeInfo(nodeinfo);

    int numDomains = virConnectListDomains(conn, domainIds, MAX_DOMAINS);
    if (numDomains == -1) {
        fprintf(stderr, "Unable to get domains: virConnectListDomains()\n");
        return -1;
    }
    fprintf(stderr, "Number of domains: %d\n", numDomains);



    // when initialized, pin each Vcpu to one Pcpu according to its ID
    unsigned char cpuMap = 0x01;
    virDomainPtr domains[MAX_DOMAINS];
    int ret;
    for (int i = 0; i< numDomains; i++){
        domains[i] = virDomainLookupByID(conn, domainIds[i]);
		ret = virDomainPinVcpu(domains[i], 0, &cpuMap, VIR_CPU_MAPLEN(nodeinfo.cpus));
	
		printf(" == vcpu %d: set to pcpu %d\n", i, cpuMap);

		cpuMap <<= 1;
		// note that the map[0] code is specialized to 8 PCPUs tops, otherwise problems. 	
		if( cpuMap >= (1 << numPcpus)){
			// roll back
            cpuMap = 0x01;
		}

		if( ret != 0 ){
			fprintf(stderr, " Unable to pin Vcpu: virDomainPinVcpu()\n");
			return -1;
		}
	}
    double* usagePercentageArrayPcpu = calloc(numPcpus, sizeof(int));
    // initialize the percentage counter
    getPercentageForPcpus(domains, usagePercentageArrayPcpu, numDomains, numPcpus);
    sleep(1);

    while (1) {
        getPercentageForPcpus(domains, usagePercentageArrayPcpu, numDomains, numPcpus);
        for(int i = 0; i < numPcpus; i++){
            printf("Pcpu %d percentage: %f\n", i, usagePercentageArrayPcpu[i]);
        }

        int maxIdx = -1, minIdx = -1;
        needScheduling(usagePercentageArrayPcpu, numPcpus, &maxIdx, &minIdx);

        if(maxIdx >= 0 && maxIdx < numPcpus && minIdx >= 0 && minIdx < numPcpus){
            cpuMap = 1 << minIdx;
            
            for(int i = 0; i < numDomains; i++){
                virVcpuInfo vcpuInfo;
                virDomainGetVcpus(domains[i], &vcpuInfo, 1, NULL, 0);
                if(vcpuInfo.cpu == maxIdx){
                    printf("rescheduling VCPU/domain %d to from PCPU %d to PCPU %d \n", i, maxIdx, minIdx);
                    virDomainPinVcpu(domains[i], 0, &cpuMap, VIR_CPU_MAPLEN(nodeinfo.cpus));
                    break;
                }
            }
        }
        sleep(refreshInterval);
    } 
}