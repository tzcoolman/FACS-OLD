#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*-------------------------------------*/
//for file mapping in Linux
#include<fcntl.h>
#include<unistd.h>
#include<sys/stat.h>
#include<sys/time.h>
#include<sys/mman.h>
#include<sys/types.h>
/*-------------------------------------*/
#include "tool.h"
#include "check.h"
#include "bloom.h"
#include "file_dir.h"
/*-------------------------------------*/
//openMP library
#include<omp.h>
/*-------------------------------------*/

int check_main (int argc, char **argv)
{
   if (argc<2)  check_help();
   
/*-------defaults for bloom filter building-------*/ 
  int opt;
  float tole_rate = 0;
  float sampling_rate = 1;
  char* ref = NULL;
  char* list = NULL;
  char* target_path = NULL;
  char* source = NULL;
  while ((opt = getopt (argc, argv, "s:t:r:o:q:l:h")) != -1) {
      switch (opt) {
          case 't':
              (optarg) && ((tole_rate = atof(optarg)), 1);
              break;
          case 's':
              (optarg) && ((sampling_rate = atof(optarg)), 1);
              break;
          case 'o':    
              (optarg) && ((target_path = optarg), 1);
              break;
          case 'q':  
              (optarg) && (source = optarg, 1);  
              break;
          case 'r':  
              (optarg) && (ref = optarg, 1);  
              break;
          case 'l':
              (optarg) && (list = optarg, 1);  
              break;
          case 'h':
              check_help();
          case '?':
              printf ("Unknown option: -%c\n", (char) optopt);
              check_help();
      } 
  } 
  return check_all (source, ref, tole_rate, sampling_rate, list, target_path);
}

int check_all (char *source, char *ref, float tole_rate, float sampling_rate, char *list, char *prefix)
{
  /*-------------------------------------*/
  char *position;
  char *detail = (char *) malloc (1000 * 1000 * sizeof (char));
  memset (detail, 0, 1000 * 1000);
  int type = 0;
  /*-------------------------------------*/
  Queue *head = NEW (Queue);
  Queue *tail = NEW (Queue);
  bloom *bl_2 = NEW (bloom);
  Queue *head2;
  head->location=NULL;
  head2 = head;
  head->next = tail;

  F_set *File_head = NEW (F_set);
  File_head = make_list (ref, list);
  /*-------------------------------------*/
  position = mmaping (source);
  type = get_parainfo (position,head);
  /*-------------------------------------*/
  while (File_head!=NULL)
    {
      load_bloom (File_head->filename, bl_2);
      if (tole_rate==0)
          tole_rate = mco_suggestion(bl_2->k_mer);
#pragma omp parallel 
      {
#pragma omp single nowait
	{
	  while (head != tail) {
#pragma omp task firstprivate(head)
	      { 

		if (head->location!=NULL)
                  {
		  if (type == 1)
		    fasta_process (bl_2, head, tail, File_head, sampling_rate,
				   tole_rate);
		  else
		    fastq_process (bl_2, head, tail, File_head, sampling_rate,
		  		   tole_rate);
		  }

	      }
	      head = head->next;
	    }
	}			// End of single - no implied barrier (nowait)
      }				// End of parallel region - implied barrier
      evaluate (detail, File_head->filename, File_head);

      File_head = File_head->next;

      head = head2;
      bloom_destroy (bl_2);
    }				//end while
  statistic_save (detail, source, prefix);
  munmap (position, strlen (position));

  //check ("test.fna","k_12.bloom","r", prefix, 1, 0.8);
  return 1;
}

/*-------------------------------------*/
void
fastq_process (bloom * bl, Queue * info, Queue *tail, F_set * File_head,
	       float sampling_rate, float tole_rate)
{

  char *p = info->location;
  char *next = NULL, *temp = NULL, *temp_piece = NULL;

  if (info->location[0] != '@') {
    return;
  } else if (info->next != tail && info->next->location!=NULL) {
    next = info->next->location;
  } else {
    next = strchr (p, '\0');
  }

  while (p != next)
    {
      //printf ("p->%0.50s\n",p);
      temp = jump (p, 2, sampling_rate);	//generate random number and judge if need to scan this read

      if (p != temp)
	{
	  p = temp;
	  continue;
	}

#pragma omp atomic
      File_head->reads_num++;

      p = strchr (p, '\n') + 1;
      if (fastq_read_check (p, strchr (p, '\n') - p, 'n', bl, tole_rate, File_head)> 0) {
#pragma omp atomic
	File_head->reads_contam++;
      }

      p = strchr (p, '\n') + 1;
      p = strchr (p, '\n') + 1;
      p = strchr (p, '\n') + 1;
    }				// outside while
  if (temp_piece)
    free (temp_piece);

}

/*-------------------------------------*/
void
fasta_process (bloom * bl, Queue * info, Queue * tail, F_set * File_head,
	       float sampling_rate, float tole_rate)
{
  #ifdef DEBUG
  printf ("fasta processing...\n");
  #endif
  char *temp_next, *next, *temp;

  if (info->location == NULL)
    return;
  else if (info->next != tail)
    next = info->next->location;
  else
    next = strchr (info->location, '\0');

  char *p = info->location;

  while (p != next)
    {
      temp = jump (p, 1, sampling_rate);	//generate random number and judge if need to scan this read

      if (p != temp)
	{
	  p = temp;
	  continue;
	}

#pragma omp atomic
      File_head->reads_num++;

      temp_next = strchr (p + 1, '>');
      if (!temp_next)
	temp_next = next;

      if (fasta_read_check (p, temp_next, 'n', bl, tole_rate, File_head) > 0)
	{
#pragma omp atomic
	  File_head->reads_contam++;
	}

      p = temp_next;
    }
}

/*-------------------------------------*/
void
evaluate (char *detail, char *filename, F_set * File_head)
{
  char buffer[200] = { 0 };
  float contamination_rate =
    (float) (File_head->reads_contam) / (float) (File_head->reads_num);

// JSON output format by default
  printf("{\n");
  printf ("\t\"total_read_count\": %lld,\n", File_head->reads_num);
  printf ("\t\"contaminated_reads\": %lld,\n", File_head->reads_contam);
  printf ("\t\"total_hits\": %lld,\n", File_head->hits);
  printf ("\t\"contamination_rate\": %f,\n", contamination_rate);
  printf ("\t\"bloom_filename\":\"%s\"\n", filename);
  printf("}\n");

#ifdef DEBUG
  strcat (detail, "Bloomfile\tAll\tContam\tcontam_rate\n");
  strcat (detail, filename);
#endif

  sprintf (buffer, "  %lld\t%lld\t%f\n", File_head->reads_num,
	   File_head->reads_contam, contamination_rate);
  strcat (detail, buffer);
}

/*-------------------------------------*/
void
statistic_save (char *detail, char *filename, char *prefix)
{
  char *save_file = NULL;
  save_file = prefix_make (filename, NULL, prefix);
  if (save_file[0]=='/')
      save_file++;
  strcat (save_file,".info");

#ifdef DEBUG
  printf ("Basename->%s\n", filename);
  printf ("Info name->%s\n", save_file);
#endif
  write_result (save_file, detail);
}
