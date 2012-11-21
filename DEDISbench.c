/* DEDISbench
 * (c) 2010 2010 U. Minho. Written by J. Paulo
 */

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64 

#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>

//duplicate distribution loader
#include "duplicatedist.c"
#include <sys/time.h>
#include <time.h>
#include <sys/wait.h>
#include <strings.h>
#include <string.h>
#include <sys/mman.h>


/*
 * Future Work
 * TODO: Make more flexible the condition of ppopulating files for read requests
 * maybe a condition if test* does not exist and populate with -d then error...
 * TODO: should we exclude the the first minutes and last form the statistics?
 * TODO: other statistics, graph generation, fexibility of results log.
 * TODO: Change the benchmark to run for N operations instead of minutes (EASY: just change while loop condition)
 * TODO: change block size
 * TODO: custom duplicates distribution -> this is easy...
 * TODO: persistent files must be in a specific location
 * 		 write files should be in a folder specified by the user like in bonnie++
 * TODO: Build failed on i386. error on open flags
 * TODO: when dist_files do not exist -> give error and exit without more screen info
 */

//type of I/O
#define READ 0
#define WRITE 1

//type of test
#define NOMINAL 2
#define PEAK 3

//type of termination
#define TIME 4
#define SIZE 5

//log feature 0=disabled 1=enabled
int logfeature;

//acess type for IO
#define SEQUENTIAL 6
#define UNIFORM 7
#define TPCC 8

//statistics variables
//throughput per second
double throughput;
//average latency
double latency;
//total operations performe
uint64_t tot_ops=0;
//Since the begin and end time of the tests are not exact about when the
//firts or last operations started or ended we register this more accuratelly
//for calculating the throughput
//exact time before the first I/O operation
uint64_t beginio;
//exact time after the last I/O operations
uint64_t endio;

//path of directory for temporary files
char tempfilespath[100];

char printfile[100];
int destroypfile=1;
int printtofile=0;

//global timeval structure for nominal tests
static struct timeval base;

//time elapsed since last I/O
long lap_time() {
	struct timeval tv;
	long delta;

	//get current time
	gettimeofday(&tv, NULL); 
	//base time - current time (in microseconds)
	delta = (tv.tv_sec-base.tv_sec)*1e6+(tv.tv_usec-base.tv_usec);

	//update base to the current time
    base = tv;

    //return delta
	return delta;
}

//sleep for quantum microseconds
void idle(long quantum) {
	usleep(quantum);
}



//we must check this
//create the file where the process will perform I/O operations
int create_pfile(int procid){

	 //create the file with unique name for process with id procid
	 char id[2];
	 sprintf(id,"%d",procid);
	 strcat(tempfilespath,"test");
	 strcat(tempfilespath,id);


	 printf("opening %s\n",tempfilespath);
	 //device where the process will write
	 int fd_test = open(tempfilespath, O_RDWR | O_LARGEFILE | O_CREAT, 0644);
	 if(fd_test==-1) {
		 perror("Error opening file for process I/O");
		 exit(0);
	 }

	 return fd_test;
}


int destroy_pfile(int procid){

	//create the file with unique name for process with id procid
	char name[120];
	char id[2];
	sprintf(id,"%d",procid);
	strcpy(name,"rm ");
	strcat(name,tempfilespath);
	strcat(name,"test");
	strcat(name,id);

	printf("performing %s\n",name);

	int ret = system(name);
	if(ret<0){
			perror("System rm failed");
	}

	return 0;
}

//populate files with content
void populate_pfiles(uint64_t filesize,int nprocs){

	int i;

    //for each process populate its file with size filesize
	//we use DD for filling a non sparse image
	for(i=0;i<nprocs;i++){
		//create the file with unique name for process with id procid
    	char name[150];
		char id[2];
		sprintf(id,"%d",i);
		char count[10];
		//printf("%llu %llu\n",filesize,filesize/1024/1024);
		sprintf(count,"%llu",(long long unsigned int)filesize/1024/1024);
		strcpy(name,"dd if=/dev/zero of=");
		strcat(name,tempfilespath);
		strcat(name,"test");
		strcat(name,id);
		strcat(name," bs=1M count=");
		strcat(name,count);


		//printf("ola mundo\n");
		//printf("%s\n",name);
		printf("populating file for process %d\n%s\n",i,name);
		int ret = system(name);
		if(ret<0){
			perror("System dd failed");
		}

	}


}

//create the log file with the results from the test
FILE* create_plog(int procid){

	//create the file with results for process with id procid
    char name[10];
	char id[2];
	sprintf(id,"%d",procid);
	strcpy(name,"result");
	strcat(name,id);

	FILE *fres = fopen(name,"w");
    return fres;

}

//run a a peak test
void process_run(int idproc, int nproc, double ratio, int duration, uint64_t number_ops, int iotype, int testtype, uint64_t totblocks){
  
  //create file where process will perform I/O
  int fd_test = create_pfile(idproc);

  //create the file with results for process with id procid
  FILE* fres=NULL;

  char name[10];
  char id[2];
  sprintf(id,"%d",idproc);
  if(logfeature==1){

	  strcpy(name,"result");
	  strcat(name,id);
	  fres = fopen(name,"w");
  }

  uint64_t* acessesarray=NULL;
  if(accesslog==1){
     	//init acesses array

	    acessesarray=malloc(sizeof(uint64_t)*totblocks);
     	uint64_t aux;
     	for(aux=0;aux<totblocks;aux++){

     		acessesarray[aux]=0;

     	}
  }

  //unique counter for each process
  //starts with value==max index at array sum
  //since duplicated content is identified by number correspondent to the indexes at sum
  //none will have a identifier bigger than this
  uint64_t u_count = duplicated_blocks+1;

  //check if terminationis time or not
  int termination_type;
  uint64_t begin;
  uint64_t end;
  struct timeval tim;
  if(duration > 0 ){
	  //Get current time to mark the beggining of the benchmark and check
	  //when it should end

	  gettimeofday(&tim, NULL);
	  begin=tim.tv_sec;
	  //the test will run for duration seconds
	  end = begin+duration;
	  termination_type=TIME;

  }
  //SIZE termination
  else{
	  begin=0;
	  end=number_ops/nproc;
	  termination_type=SIZE;
  }

  if (accesstype==TPCC){
	  initialize_nurand(totblocks);
  }

  //variables for nominal tests
  //getcurrent time and put in global variable base
  gettimeofday(&base, NULL);
  //time elapsed (us) for all operations.
  //starts with value 1 because the value must be higher than 0.
  //the nominal rate will then adjust to the base value and the
  //overall throughput will not be affected.
  double time_elapsed=1;

  //while bench time has not ended
  //TODO: if we change this condition to while(Nrops<X) we could
  //change the test stop condition to pperform Xkb of I/o instead of time

  while(begin<end){

   //for nominal testes only
   //number of operations performed for all processes
   //since we are running N processes concurrently at the same I/O rate
   //the number of operations must be multiplied by all
   double ops_proc=tot_ops*nproc;

   assert(ops_proc>=0);
   assert(time_elapsed>0);


   //IF the the test is peak or if it is NOMINAL and we are below the expected rate
   if(testtype==PEAK || ops_proc/time_elapsed<ratio){

     //memory block
	 char buf[block_size];

	 //If it is a write test then get the content to write and
	 //populate buffer with the content to be written
	 if(iotype==WRITE){


	   //initialize the buffer with duplicate content
	   int bufp = 0;
	   for(bufp=0;bufp<block_size;bufp++){
		   buf[bufp] = 'a';
	   }

	   //get the content
	   uint64_t contwrite =	get_writecontent(&u_count);

	   //contwrite is the index of sum where the block belongs
	   //put in statistics this value ==1 to know when a duplicate is found
	   if(contwrite<duplicated_blocks){
		   statistics[contwrite]++;
		   if(statistics[contwrite]>1){
			   dupl++;
		   }
		   else{
			   uni++;
		   }
	   }

	   //if the content to write is unique write to the buffer
	   //the unique counter of the process + "string"  + process id
	   //to be unique among processes the string invalidates to have
	   //an identical number from other oprocess
	   if(contwrite>duplicated_blocks){
	       	  sprintf(buf,"%llu pid %d", (long long unsigned int)contwrite,idproc);
	       	  uni++;
	       	  //uni referes to unique blocks meaning that
	       	  // also counts 1 copy of each duplicated block
	       	  //zerodups only refers to blocks with only one copy (no duplicates)
	       	  zerod++;
	       	  *zerodups=*zerodups+1;
	   }
	   //if it is duplicated write the result (index of sum) returned
	   //into the buffer
	   else{
	       	  sprintf(buf,"%llu", (long long unsigned int)contwrite);
	   }

	   uint64_t iooffset;

	   if(accesstype==SEQUENTIAL){
		   //Get the position to perform I/O operation
		   iooffset = get_ioposition_seq(totblocks,tot_ops);
	   }else{
		   if(accesstype==UNIFORM){
			   //Get the position to perform I/O operation
			   iooffset = get_ioposition_uniform(totblocks);
		   }
		   else{
			   //Get the position to perform I/O operation
			  iooffset = get_ioposition_tpcc(totblocks);

		   }
	   }
	  if(accesslog==1){
		  acessesarray[iooffset/block_size]++;
	   }


       //get current time for calculating I/O op latency
       gettimeofday(&tim, NULL);
       uint64_t t1=tim.tv_sec*1000000+(tim.tv_usec);

       int res = pwrite(fd_test,buf,block_size,iooffset);

       //latency calculation
       gettimeofday(&tim, NULL);
       uint64_t t2=tim.tv_sec*1000000+(tim.tv_usec);
       uint64_t t2s = tim.tv_sec;

       if(res ==0 || res ==-1)
           perror("Error writing block ");

       if(beginio==-1){
    	   beginio=t1;
       }

       latency+=(t2-t1);
       endio=t2;

       if(logfeature==1){
         //write in the log the operation latency
         fprintf(fres,"%llu %llu\n", (long long unsigned int) t2-t1, (long long unsigned int)t2s);
       }

	}
	//If it is a read benchmark
	else{

		uint64_t iooffset;

		if(accesstype==SEQUENTIAL){
				   //Get the position to perform I/O operation
				   iooffset = get_ioposition_seq(totblocks,tot_ops);
			   }else{
				   if(accesstype==UNIFORM){
					   //Get the position to perform I/O operation
					   iooffset = get_ioposition_uniform(totblocks);
				   }
				   else{
					   //Get the position to perform I/O operation
					  iooffset = get_ioposition_tpcc(totblocks);

				   }
			   }
		if(accesslog==1){
			acessesarray[iooffset/block_size]++;
		}

		//get current time for calculating I/O op latency
		gettimeofday(&tim, NULL);
		uint64_t t1=tim.tv_sec*1000000+(tim.tv_usec);

		uint64_t res = pread(fd_test,buf,block_size,iooffset);

		//latency calculation
		gettimeofday(&tim, NULL);
		uint64_t t2=tim.tv_sec*1000000+(tim.tv_usec);
		uint64_t t2s = tim.tv_sec;

		if(res ==0 || res ==-1)
		     perror("Error reading block ");

		if(beginio==-1){
		   	   beginio=t1;
		}

		latency+=(t2-t1);
		endio=t2;

		if(logfeature==1){
		  //write in the log the operation latency
		  fprintf(fres,"%llu %llu\n", (long long unsigned int) t2-t1, (long long unsigned int) t2s);
		}
     }

	 //One more operation was performed
	 tot_ops++;
	 if(termination_type==SIZE){
		   begin++;
	 }

   }
   else{
       //if the test is nominal and the I/O throughput is higher than the
	   //expected ration sleep for a while
	   idle(4000);
   }


   //add to the total time the time elapsed with this operation
   time_elapsed+=lap_time();

   //DEBUG;
   if((tot_ops%100000)==0){
      printf("Process %d has reached %llu operations\n",idproc, (long long unsigned int) tot_ops);
   }

   //update current time
   gettimeofday(&tim, NULL);
   if(termination_type==TIME){
	   begin=tim.tv_sec;
   }

  }

  if(logfeature==1){
	  fclose(fres);

  }
  close(fd_test);

  //calculate average latency milisseconds
  latency=(latency/tot_ops)/1000.0;

  throughput=(tot_ops/((endio-beginio)/1.0e6));

  /*
  //inserts statistics list into berkeleyDB in order to sum with all processes and then calculate
  //the total
  printf("before generating dist file\n");
  init_db(STATDB,dbpstat,envpstat);
  gen_totalstatistics(dbpstat,envpstat);
  close_db(dbpstat,envpstat);


  //print a distribution file like the one given in input for zero duplicate blocks written
  printf("before generating dist file\n");
  init_db(DISTDB,dbpdist,envpdist);
  gen_zerodupsdist(dbpdist,envpdist);
  close_db(dbpdist,envpdist);
  printf("Process %d:\nUnique Blocks Written %llu\nZero Copies Blocks Written %llu\nDuplicated Blocks Written %llu\nTotal I/O operations %llu\nThroughput: %.3f blocks/second\nLatency: %.3f miliseconds\n",idproc,(long long unsigned int)uni,(long long unsigned int)zerod,(long long unsigned int)dupl,(long long unsigned int)tot_ops,throughput,latency);
*/
  printf("Process %d:\nUnique Blocks Written %llu\nDuplicated Blocks Written %llu\nTotal I/O operations %llu\nThroughput: %.3f blocks/second\nLatency: %.3f miliseconds\n",idproc,(long long unsigned int)uni,(long long unsigned int)dupl,(long long unsigned int)tot_ops,throughput,latency);

  if(printtofile==1){

	  FILE* pf=fopen(printfile,"a");
	  fprintf(pf,"Process %d:\nUnique Blocks Written %llu\nDuplicated Blocks Written %llu\nTotal I/O operations %llu\nThroughput: %.3f blocks/second\nLatency: %.3f miliseconds\n",idproc,(long long unsigned int)uni,(long long unsigned int)dupl,(long long unsigned int)tot_ops,throughput,latency);
	  fclose(pf);

  }

  if(accesslog==1){

	  	 strcat(accessfilelog,id);

	  	 //print distribution file
	     FILE* fpp=fopen(accessfilelog,"w");
	     uint64_t iter;
	     for(iter=0;iter<totblocks;iter++){
	    	 fprintf(fpp,"%llu %llu\n",(unsigned long long int) iter, (unsigned long long int) acessesarray[iter]);
	     }

	     fclose(fpp);
         //init acesses array
         free(acessesarray);
   }


}


void launch_benchmark(int nproc, uint64_t totblocks, int time_to_run, uint64_t number_ops, double ratio,uint64_t seed,int iotype,int testtype){


	int i;
	//launch processes for each file bench
	int nprocinit=nproc;

	pid_t *pids=malloc(sizeof(pid_t)*nproc);


	for (i = 0; i < nproc; ++i) {
	  if ((pids[i] = fork()) < 0) {
	    perror("error forking");
	    abort();
	  } else if (pids[i] == 0) {
		  printf("loading process %d\n",i);

		  //init random generator
		  //if the seed is always the same the generator generates the same numbers
		  //for each proces the seed = seed + processid or all the processes would
		  //generate the same load
		  init_rand(seed+i);

		  //work performed by each process
		  process_run(i, nproc, ratio, time_to_run, number_ops, iotype, testtype, totblocks);
		  //sleep(10);
	     exit(0);
	  }
	}

	/* Wait for children to exit. */
	int status;
	pid_t pid;
	while (nproc > 0) {
	  pid = wait(&status);
	  printf("Terminating process with PID %ld exited with status 0x%x.\n", (long)pid, status);
	  --nproc;
	  // TODO(pts): Remove pid from the pids array.
	}
	free(pids);



	if(destroypfile==1){
	printf("Destroying temporary files\n");
		for (i = 0; i < nprocinit; i++) {

			destroy_pfile(i);

		}
	}

	printf("Exiting benchmark\n");

}


int loadmmap(uint64_t **mem,uint64_t *sharedmem_size,int *fd_shared){

	 //Name of shared memory file
	 int result;


	  //size of shared memory structure
	  *sharedmem_size = sizeof(uint64_t)*(duplicated_blocks*3+1);


	  *fd_shared = open("sharedmemstats", O_RDWR | O_CREAT, (mode_t)0600);
	  if (*fd_shared == -1) {
	    perror("Error opening file for writing");
	    exit(EXIT_FAILURE);
	  }

	  /* Stretch the file size to the size of the (mmapped) array of ints
	   */
	  result = lseek(*fd_shared, *sharedmem_size-1, SEEK_SET);
	  if (result == -1) {
	      close(*fd_shared);
	      perror("Error calling lseek() to 'stretch' the file");
	      exit(EXIT_FAILURE);
	  }

	  /* Something needs to be written at the end of the file to
	   * have the file actually have the new size.
	   * Just writing an empty string at the current file position will do.
	   *
	   * Note:
	   *  - The current position in the file is at the end of the stretched
	   *    file due to the call to lseek().
	   *  - An empty string is actually a single '\0' character, so a zero-byte
	   *    will be written at the last byte of the file.
	   */
	   result = write(*fd_shared, "", 1);
	   if (result != 1) {
	      close(*fd_shared);
	      perror("Error writing last byte of the file");
	      exit(EXIT_FAILURE);
	   }


	  // Now the file is ready to be mapped to memory
	  *mem = (uint64_t*)mmap(0, *sharedmem_size, PROT_READ | PROT_WRITE, MAP_SHARED, *fd_shared, 0);
	  if (*mem == MAP_FAILED) {
	    close(*fd_shared);
	    perror("Error mmapping the file");
	    exit(EXIT_FAILURE);
	  }

	  // Now assign the memory region to each variable
	  statistics = *mem;
	  *mem=*mem+duplicated_blocks;
	  sum =*mem;
	  *mem=*mem+duplicated_blocks;
	  stats=*mem;
	  *mem=*mem+duplicated_blocks;
	  zerodups = *mem;

	  return 0;
}

int closemmap(uint64_t **mem,uint64_t *sharedmem_size,int *fd_shared){

	/*
	if (munmap(*mem, *sharedmem_size) == -1) {
		perror("Error un-mmapping the file");
		// Decide here whether to close(fd_shared) and exit() or not. Depends...
	}*/

	close(*fd_shared);

	return 0;
}

void usage(void)
{
	printf("Usage:\n");
	printf(" -p or -n<value>\t(Peak or Nominal Bench with throughput rate of N operations per second)\n");
	printf(" -w or -r\t\t(Write or Read Bench)\n");
	printf(" -t<value> or -s<value>\t(Benchmark duration (-t) in Minutes or amount of data to write (-s) in MB)\n");
	printf(" -h\t\t\t(Help)\n");
	exit (8);
}

void help(void){

	printf(" Help:\n\n");
	printf(" -p or -n<value>\t(Peak or Nominal Bench with throughput rate of N operations per second)\n");
	printf(" -w or -r\t\t(Write or Read Bench)\n");
	printf(" -t<value> or -s<value>\t(Benchmark duration (-t) in Minutes or amount of data to write (-s) in MB)\n");
	printf("\n Optional Parameters\n\n");
	printf(" -a<value>\t\t(Access pattern for I/O operations: 0-Sequential, 1-Random Uniform, 2-TPCC random default:2)\n");
	printf(" -c<value>\t\t(Number of concurrent processes default:4)\n");
	printf(" -f<value>\t\t(Size of process file in MB default:2048 MB)\n");
	printf(" -l\t\t\t(Enable file log feature for results)\n");
	printf(" -d<value>\t\t(choose the directory where DEDISbench writes data)\n");
	printf(" -e or -y\t\t(Enable or disable the population of process files before running DEDISbench. Only Enabled by default for read tests)\n");
	printf(" -b<value>\t(Size of blocks for I/O operations in Bytes default: 4096)\n");
	printf(" -g<value>\t\t(Input File with duplicate distribution. default: dist_personalfiles \n");
	printf("\t\t\t DEDISbench can simulate three real distributions extracted respectively from an Archival, Personal Files and High Performance Storage\n");
	printf("\t\t\t For choosing these distributions the <value> must be dist_archival, dist_personalfiles or dist_highperf respectively\n");
	printf("\t\t\t For creating a custom file or finding additional info on the available distributions please check the README file.)\n");
	printf(" -v<value>\t\t(Seed for random generator default:current time)\n");
	printf(" -o<value>\t\t(generate an output log with the distribution actually generated by the benchmark)\n");
	printf(" -k<value>\t\t(generate an output log with the access pattern generated by the benchmark)\n");
	printf(" -z\t\t\t(Disable the destruction of process temporary files generated by the benchmark)\n");
	exit (8);

}

int main(int argc, char *argv[]){


	//necessary parameters
	//I/O type READ or WRITE
    int iotype=-1;
    //TEST type PEAK or NOMINAL
    int testtype=-1;
    //ratio for I/O throughput ops/s
    double ratio = -1;
    //duration of benchmark (minutes)
    int time_to_run = 0;
    //size of benchmark
    uint64_t number_ops =0;

    int termination_type=-1;
    //Optional parameters
    //duplicates distribution file (default homer file DFILE)
    //Number of processes (default 4)
	int nproc =4;
	//File size for process in MB (default 2048)
	uint64_t filesize = 2048LLU;
	// block size in KB (default 4)
	block_size=4096LL;

	//default seed is be given by current time
	struct timeval tim;
	gettimeofday(&tim, NULL);
	uint64_t seed=tim.tv_sec*1000000+(tim.tv_usec);

	//logging of I/O is disabled statistics are calculated in memory
	logfeature=0;

	//populate feature, by default is off unless it is a read test
	int populate=-1;

	//distribution file path
	char distfile[100];
	int distf=0;

	//output dirstibution file
	char outputfile[100];
	int distout=0;
	accesstype=TPCC;
	int auxtype;


   	while ((argc > 1) && (argv[1][0] == '-'))
	{
		switch (argv[1][1])
		{
			case 'p':
				//Test if -n is not being used also
				if(testtype!=NOMINAL)
					testtype=PEAK;
				else{
				  printf("Cannot use both -p and -n\n");
				  usage();
				}
				break;
			case 'n':
				//test if -p is not being used also
				if(testtype!=PEAK)
					testtype=NOMINAL;
				else{
				  printf("Cannot use both -p and -n\n\n");
				  usage();
				}
				//test if the value from -n is higher than 0
				ratio=atoi(&argv[1][2]);
				break;
			case 'w':
				if(iotype!=READ)
					iotype=WRITE;
				else{
				    printf("Cannot use both -r and -w\n\n");
					usage();}
				break;
			case 'r':
				if(iotype!=WRITE)
					iotype=READ;
				else{
				  printf("Cannot use both -p and -n\n\n");
				  usage();}
				break;
			case 't':
				if(termination_type!=SIZE){
					termination_type=TIME;
					time_to_run=atoi(&argv[1][2]);
				}
				else{
					printf("Cannot use both -t and -s\n\n");
					usage();
				}
				break;
			case 's':
				if(termination_type!=TIME){
					termination_type=SIZE;
					number_ops=atoll(&argv[1][2]);
				}
				else{
					printf("Cannot use both -t and -s\n\n");
					usage();
				}
				break;
			case 'a':
				auxtype=atoll(&argv[1][2]);
				if(auxtype==0)
					accesstype=SEQUENTIAL;
				if(auxtype==1)
					accesstype=UNIFORM;
				if(auxtype==2)
					accesstype=TPCC;

				if(auxtype!=0 && auxtype!=1 && auxtype!=2)
					perror("Unknown type of pattern acess for I/O operations");
				break;
			case 'f':
				filesize=atoll(&argv[1][2]);
				break;
			case 'b':
				block_size=atoll(&argv[1][2]);
				break;
			case 'v':
				seed=atof(&argv[1][2]);
				break;
			case 'c':
				nproc=atoi(&argv[1][2]);
				break;
			case 'h':
				help();
				break;
			case 'g':
				strcpy(distfile,&argv[1][2]);
				distf=1;
				break;
			case 'o':
				strcpy(outputfile,&argv[1][2]);
				distout=1;
				break;
			case 'k':
				accesslog=1;
				strcpy(accessfilelog,&argv[1][2]);
				break;
			case 'd':
				strcpy(tempfilespath,&argv[1][2]);
				break;
			case 'j':
				printtofile=1;
				strcpy(printfile,&argv[1][2]);
				break;
			case 'l':
				logfeature=1;
				break;
			case 'z':
				destroypfile=0;
				break;
			case 'e':
				if(populate!=0){
					populate=1;
				}
				else{
					printf("Cannot use both -e and -d\n\n");
					usage();
				}
				break;
			case 'y':
				if(populate!=1){
					populate=0;
				}
				else{
					printf("Cannot use both -e and -y\n\n");
					usage();
				}
				break;
			default:
				printf("Wrong Argument: %s\n", argv[1]);
				usage();
				exit(0);
				break;
		}

		++argv;
		--argc;
	}


	//test if iotype is defined
	if(iotype!=WRITE && iotype!=READ){
		printf("missing -w or -r\n\n");
		usage();
		exit(0);
	}
	//test if testype is defined
	if(testtype!=PEAK && testtype!=NOMINAL){
		printf("missing -p or -n<value>\n\n");
		usage();
		exit(0);
	}
	//test if testype is defined
	if(termination_type!=TIME && termination_type!=SIZE){
			printf("missing -t or -s<value>\n\n");
			usage();
			exit(0);
	}
	//test if time_to_run is defined and > 0
	if(termination_type==TIME && time_to_run<0){
		printf("missing -t<value> with value higher than 0\n\n");
		usage();
		exit(0);
	}
	//test if numbe_ops is defined and > 0
	if(termination_type==SIZE && number_ops<0){
		printf("missing -s<value> with value higher than 0\n\n");
		usage();
		exit(0);
	}

	//test if filesize > 0
	if(filesize<=0){
			printf("missing -f<value> with value higher than 0\n\n");
			usage();
			exit(0);
	}
	//test if ratio >0 and defined
	if(testtype==NOMINAL && ratio<=0){
			printf("missing -n<value> with value higher than 0\n\n");
			usage();
			exit(0);
	}

	//test if blocksize >0
	if(block_size<=0){
		printf("block size value must be higher than 0\n");
		usage();
		exit(0);
	}

	//convert to to ops/microsecond
	ratio=ratio/1e6;
	//convert to bytes
	filesize=filesize*1024*1024;
    //total blocks to be addressed at file
    uint64_t totblocks = filesize/block_size;

    //convert time_to_run to seconds
    if(termination_type==TIME)
    	time_to_run=time_to_run*60;

    if(termination_type==SIZE)
    	number_ops=(number_ops*1024*1024)/block_size;

    uint64_t **mem=malloc(sizeof(uint64_t*));
    uint64_t sharedmem_size;
    int fd_shared;


    //check if a distribution file was given as parameter
    if(distf==1){

    	//get global information about duplicate and unique blocks
    	printf("loading duplicates distribution %s...\n",distfile);
    	get_distibution_stats(distfile);

        loadmmap(mem,&sharedmem_size,&fd_shared);
    	*zerodups=0;

    	//load duplicate array for using in the benchmark
    	load_duplicates(distfile);
    }
    else{
    	//get global information about duplicate and unique blocks
    	printf("loading duplicates distribution %s...\n",DFILE);
    	get_distibution_stats(DFILE);

    	loadmmap(mem,&sharedmem_size,&fd_shared);
    	*zerodups=0;

    	//load duplicate array for using in the benchmark
    	load_duplicates(DFILE);
    }


	//printf("distinct blocks %llu number unique blocks %llu number duplicates %llu\n",(long long unsigned int)total_blocks, (long long unsigned int)unique_blocks,(long long unsigned int)duplicated_blocks);

	load_cumulativedist();

    //writes can be performed over a populated file (populate=1)
    //this functionality can be disabled if the files are already populated (populate=0)
    //Or we can verify if the files already exist and ask?
    if((iotype==READ && populate!=0) || populate==1){
    	populate_pfiles(filesize,nproc);
    }

    //initialize beginio variable with -1;
    beginio=-1;
    
    //init database for generating final distribution
    dbpdist=malloc(sizeof(DB *));
    envpdist=malloc(sizeof(DB_ENV *));

    remove_db(DISTDB,dbpdist,envpdist);
/*
    //init database for merging statistics lists
    dbpstat=malloc(sizeof(DB *));
    envpstat=malloc(sizeof(DB_ENV *));

    remove_db(STATDB,dbpstat,envpstat);
*/

    launch_benchmark(nproc,totblocks,time_to_run,number_ops,ratio,seed,iotype,testtype);

    if(distout==1){
    	init_db(DISTDB,dbpdist,envpdist);
    	gen_outputdist(dbpdist,envpdist);

    	//print distribution file
    	FILE* fpp=fopen(outputfile,"w");
    	print_elements_print(dbpdist, envpdist,fpp);
    	fclose(fpp);

    	close_db(dbpdist,envpdist);
    	remove_db(DISTDB,dbpdist,envpdist);

    }
    closemmap(mem,&sharedmem_size,&fd_shared);






    /*
    printf("running after wait\n");

    printf("merging statistics for each process\n");
    init_db(STATDB,dbpstat,envpstat);
    init_db(DISTDB,dbpdist,envpdist);

    gen_statisticsdist(dbpstat,envpstat, dbpdist,envpdist);

    //print distribution file
    FILE* fpp=fopen("dedisbenchdist","w");
    print_elements_print(dbpdist, envpdist,fpp);
    fclose(fpp);

    printf("closing and removing databases\n");
    close_db(dbpstat,envpstat);
    remove_db(DISTDB,dbpstat,envpstat);
    close_db(dbpdist,envpdist);
    remove_db(DISTDB,dbpdist,envpdist);
*/


  return 0;
  
}



