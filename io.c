/* DEDISbench
 * (c) 2010 2010 U. Minho. Written by J. Paulo
 */

#include "io.h"
#include "random.h"


int init_io(struct user_confs *conf, int procid){

  //init random generator
  //if the seed is always the same the generator generates the same numbers
  //for each proces the seed = seed + processid or all the processes would
  //generate the same load
  init_rand(conf->seed+procid);

  init_ioposition(conf);

  return 0;
}

uint64_t write_request(char* buf, struct user_confs *conf, struct duplicates_info *info, struct stats *stat, int idproc, uint64_t *idwrite){

  *idwrite = get_writecontent(buf, conf, info, stat, idproc);

  return get_ioposition(conf, stat, idproc);
}

uint64_t read_request(struct user_confs *conf, struct stats *stat, int idproc){

  return get_ioposition(conf, stat, idproc);

}

