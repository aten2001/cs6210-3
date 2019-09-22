#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libvirt/libvirt.h>

#define MAX_DOMAINS 512

#define lowerThreshold 100
#define higherThreshold 200


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
	unsigned long long* unusedVirMemArray = calloc(numDomains, sizeof(unsigned long long));
	unsigned long long* availableVirMemArray = calloc(numDomains, sizeof(unsigned long long));
	unsigned long long* totalVirMemArray = calloc(numDomains, sizeof(unsigned long long));
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

            needRefresh = 0;
			for(int j = 0; j < numSubfields; j++){ 
				printf(" ++++++\n");
				if(memStat[j].tag == VIR_DOMAIN_MEMORY_STAT_AVAILABLE){
					availableVirMemArray[i] = memStat[j].val / 1024;
					printf(" domain %d: Available Memory-> %llu\n", i, availableVirMemArray[i]);

				}else if(memStat[j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED){
					unsigned long long unusedMem = memStat[j].val / 1024;
					printf(" domain %d: Unused Memory-> %llu\n", i, unusedMem);
                    unusedVirMemArray[i] = unusedMem;
                    int deflateIdx = -1;
					if( unusedMem < lowerThreshold && inited==1 ){ 
                        unsigned long long curHighestUnusedMem = 0;
						// deflate the domain with the most unused memory
						for(int k = 0; k < numDomains; k++){
							if(k == i){
								continue;
							}
							unsigned long long curUnusedMem = unusedVirMemArray[k];
							if(curUnusedMem > curHighestUnusedMem){
								curHighestUnusedMem = curUnusedMem;	
								deflateIdx = k;
							}						
						}

                        unsigned long long memShouldAssign = 0;
						if((unusedVirMemArray[deflateIdx]/2) < lowerThreshold){
							printf(" domain %d: reassigning mem from hypervisor directly\n", i);
							memShouldAssign = totalVirMemArray[i] - unusedMem+ (lowerThreshold + higherThreshold) / 2;	
						}
						else{
							memShouldAssign = totalVirMemArray[i] + (unusedVirMemArray[deflateIdx]/2);
							printf(" domain %d: reassigning from domain %d\n", i, deflateIdx);
		
							virDomainSetMemory(domainIds[deflateIdx], (totalVirMemArray[deflateIdx] - unusedVirMemArray[deflateIdx] / 2)* 1024);
						}


						// assign memory to domain i
						virDomainSetMemory(domainIds[i], memShouldAssign *1024);
						printf(" !! Assigning %llu to the starved domain --- %d\n", memShouldAssign, ret);

						// refresh stats
                        needRefresh = 1;
						inited = 0;
					}else if(unusedMem > higherThreshold && inited == 1) {
						printf(" !! %d: Reassigning back to the hypervisor\n", i);
						ret = virDomainSetMemory(domainIds[i], (totalVirMemArray[i] - unusedMem + lowerThreshold + 50 )*1024)  ;
						if( ret == -1){
							printf(" !!!!! Pin failed, exiting\n");
						}
						needRefresh = 1;
						inited = 0;
					}

				}else if(memStat[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON){
					totalVirMemArray[i] = memStat[j].val/ 1024;
					printf(" domain %d: Balloon stats-> %llu\n", i, memStat[j].val/ 1024);
				}
			} 
			// need to refresh stats
			if(needRefresh == 1){
				inited = 0;
				break;
			}	
	
		} // for domains

		if(needRefresh == 0 && inited == 0){
			printf(" Stats refreshed\n");
			inited = 1;
		}else{
            usleep(refreshInterval * 1000000);
        }
	}
	return 0;
}