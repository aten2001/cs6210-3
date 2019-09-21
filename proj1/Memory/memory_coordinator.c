#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libvirt/libvirt.h>

#define MAX_DOMAINS 512

#define lowerThreshold 100
#define higherThreshold 250


void printNodeInfo(virNodeInfo info){
    printf(" ====================================================\n");
    printf(" NodeInfo:\n");
	printf(" model == %s\n", info.model);
	printf(" total memory == %lu kb\n", info.memory);
    printf(" ====================================================\n");
}

int main(int argc, char** argv){
    if(argc != 2){
        fprintf(stdout, "Usage: ./memory refresh_interval\n");
        return -1;
    }

	int refreshInterval = atoi(argv[1]);
    if(refreshInterval < 0){
        fprintf(stdout, "refresh_interval should be an integer bigger than zero\n");
        return -1;
    }

    const char *hostName = "qemu:///system";
    virConnectPtr conn = virConnectOpen(hostName);
    if (!conn){
        fprintf(stderr, "Unable to connect: virConnectOpen()\n");
        return -1;
    }

	// get node info about the host machine
    virNodeInfo nodeInfo;
	int ret = virNodeGetInfo(conn, &nodeInfo);
	if (ret){
		fprintf(stderr, "Unable to get node info: virNodeGetInfo()\n");
		return ret;
	}
	unsigned long totoalPhysicalMem = nodeInfo.memory;
	printNodeInfo(nodeInfo);

    // get num of domains
    virDomainPtr *domainIds;
    int numDomains = virConnectListAllDomains(conn, &domainIds, VIR_CONNECT_LIST_DOMAINS_RUNNING);
    if (numDomains == -1){
        fprintf(stderr, "Unable to get domains: virConnectListDomains()\n");
        return -1;
    }
    fprintf(stderr, "Number of domains: %d\n", numDomains);

	// initialize the data for the memory usage 
	unsigned long long* availableVirMemArray = calloc(numDomains, sizeof(unsigned long long));
	// domain mem stat update every second
	for(int i = 0; i < numDomains; i++){
		ret = virDomainSetMemoryStatsPeriod(domainIds[i], 1, VIR_DOMAIN_AFFECT_LIVE);
	
	}

	sleep(1);
    int inited = 0;
    int needRefresh = 0;
	
	// take a look at inital values for Memory Stats
	while(1){
		printf(" ==========================================================\n");
		for(int i = 0; i < numDomains; i++){// have every domain take stats every second
            virDomainMemoryStatStruct memStat[VIR_DOMAIN_MEMORY_STAT_NR];
			int numSubfields = virDomainMemoryStats(domainIds[i], memStat, VIR_DOMAIN_MEMORY_STAT_NR, 0);

			if (numSubfields <= 0){
				fprintf(stderr, " Unable to get memory statistics: virDomainMemoryStats()\n");
				return numSubfields;
			}

			printf("\n + Analysing domain %d, with available memstats %d/%d requested\n", i, numSubfields, VIR_DOMAIN_MEMORY_STAT_NR);
            needRefresh = 0;
			for(int j = 0; j < numSubfields; j++){ 
					
				if(memStat[j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED){
					unsigned long long unusedMem = memStat[j].val / 1024;
					printf(" ++ %d: Unused Memory-> %llu\n", i, unusedMem);

				}else if(memStat[j].tag == VIR_DOMAIN_MEMORY_STAT_AVAILABLE){
					unsigned long long availableMem = memStat[j].val / 1024;
					printf(" ++ %d: Available Memory stats-> %llu\n", i, availableMem);
                    availableVirMemArray[i] = availableMem;
                    int deflateIdx = -1;
					// *********** CLAIMING MEMORY FOR A STARVED PROCESS ******************
					if( availableMem < lowerThreshold && inited==1 ){ // should probably do something about this
                        unsigned long long curHighestAvailableMem = 0;
						// who should we deflateIdx? 
						for(int k = 0; k < numDomains; k++){
							if(k == i){
								continue;
							}
							unsigned long long curAvailableMem = availableVirMemArray[k];
							if(curAvailableMem > curHighestAvailableMem){

								printf(" %d: %llu: %llu > %llu\n", k, availableVirMemArray[k], curAvailableMem, curHighestAvailableMem);
								curHighestAvailableMem = curAvailableMem;	
								deflateIdx = k;
							}						
						}
						// get defaltes memory usage and cut it in half. 
						// availableMem is a quarter of the balloon availablility from the thing we're deflating

                        unsigned long long memShouldAssign = 0;
						if(availableVirMemArray[deflateIdx] - (availableVirMemArray[deflateIdx]/2) < lowerThreshold/2){
							printf(" !! %d: Reassigning memory from the hypervisor, not another vm\n", i);
							memShouldAssign = higherThreshold/2;	
						}
						else{
							memShouldAssign = availableVirMemArray[i] + (availableVirMemArray[deflateIdx]/2);
							printf(" !! %d: Reassigning %llu kb/%llu  of %d \n", i, availableVirMemArray[deflateIdx]/2, availableMem, deflateIdx);
		
							ret = virDomainSetMemory(domainIds[deflateIdx], (availableVirMemArray[deflateIdx] / 2)* 1024);
							if( ret == -1){
								printf(" !!!!! Pin failed, exiting\n");
								return 1;
							}
						}


						// assign that amount to the domain that needs the memory
						ret = virDomainSetMemory(domainIds[i], memShouldAssign *1024);
						printf(" !! Assigning %llu to the starved domain --- %d\n", memShouldAssign, ret);
						if( ret == -1){
							printf(" !!!!! Pin failed, exiting\n");
							return 1;
						}
                        needRefresh = 1;
					}
					// *********** CLAIMING MEMORY FROM A GREEDY PROCESS to the HV
					else if(availableMem > higherThreshold && inited == 1) {
						printf(" !! %d: Reassigning back to the hypervisor\n", i);
						ret = virDomainSetMemory(domainIds[i], (availableMem - higherThreshold)*1024)  ;
						if( ret == -1){
							printf(" !!!!! Pin failed, exiting\n");
						}
						needRefresh = 1;
					}

				}else if(memStat[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON){
					printf(" ++ %d: Balloon stats-> %llu\n", i, memStat[i].val/ 1024);
				}
			} // for items in memInfo	
			if(needRefresh == 1){// we need to reclaculate everything
				inited = 0;
				break;
			}	
	
		} // for domains

		if(needRefresh == 0 && inited == 0){
			printf(" Inited\n");
			inited = 1;
		}else{
            usleep(refreshInterval * 1000000);
        }
	}// while 1




	return 0;
}