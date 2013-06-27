#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "uthash.h"
#include "cfengine.h"

#define __USE_GNU

#include <dlfcn.h> // for dlsym()

/*
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *
 *
 *  Cfe-profiler: a CFEngine profiler - Loic Pefferkorn <loic-cfe@loicp.eu>
 *
 *  http://www.loicp.eu/cfe-profiler
 *
 */

const int MAX_HASH_LEN = 1024;

typedef struct _bundle_stats bundle_stats;
struct _bundle_stats {
	char *key;      // Hash of the 6th next fields
	char *namespace;
	char *bundletype;
	char *bundle;
	char *agentsubtype;
	uint64_t ticks;
	UT_hash_handle hh;
};

bundle_stats *bundles_stats = NULL;

uint64_t rdtsc(void);
void add_bundle_call(Promise *pp, uint64_t ticks);
int sort_by_ticks(bundle_stats *a, bundle_stats *b);

// rtdsc() credits: http://www.cs.wm.edu/~kearns/001lab.d/rdtsc.html
uint64_t rdtsc(void)
{
  //uint64_t x;
  unsigned a, d;
  __asm__ volatile("rdtsc" : "=a" (a), "=d" (d));
  return ((uint64_t)a) | (((uint64_t)d) << 32);;
}

// For each bundle, add an entry to a global hash
void add_bundle_call(Promise *pp, uint64_t ticks) {
  bundle_stats *bs = NULL;
  char *hash = NULL;

  hash = malloc(MAX_HASH_LEN);
  if (hash == NULL)
    perror("Cannot allocate memory for hash\n");

  snprintf(hash, MAX_HASH_LEN, "%s%s%s",
      pp->namespace,
      pp->bundletype,
      pp->bundle);

  HASH_FIND_STR(bundles_stats, hash, bs);

  if (bs == NULL) {
    bs = malloc(sizeof(*bs));
    bs->key = hash;
    bs->namespace = strdup(pp->namespace);
    bs->bundletype = strdup(pp->bundletype);
    bs->bundle = strdup(pp->bundle);
    bs->ticks = ticks;
    HASH_ADD_KEYPTR(hh, bundles_stats, bs->key, strlen(bs->key), bs);
  } else {
    bs->ticks += ticks;
    free(hash);
  }
}

// Display bundle execution statistics
void print_stats() {

  bundle_stats *bs = NULL;
  uint64_t total_ticks = 0;
  float p;

  // Get CPU ticks used overall
  for(bs=bundles_stats; bs != NULL; bs=(bundle_stats *)(bs->hh.next)) {
    total_ticks += bs->ticks;
  }

  printf("\nCfe-profiler-0.1: a CFEngine profiler - http://www.loicp.eu/cfe-profiler\n");
  printf("\n*** Sorted by ticks - total CPU ticks: %lu ***\n", total_ticks);
  printf("%6s %10s %10s %20s\n",
      "%CPU","Namespace", "Type", "Bundle");

  HASH_SORT(bundles_stats, sort_by_ticks);

  for(bs=bundles_stats; bs != NULL; bs=(bundle_stats *)(bs->hh.next)) {
    p = (bs->ticks / (float) total_ticks) * 100;

    printf("%6.2f %10s %10s %20s\n",
        p,
        bs->namespace,
        bs->bundletype,
        bs->bundle);
  }
}

// Helper function to sort hash by ticks
int sort_by_ticks(bundle_stats *a, bundle_stats *b) {
  return (a->ticks <= b->ticks);
}

// Our version of ExpandPromise(): collect informations about promise, then run real ExpandPromise
void ExpandPromise(enum cfagenttype agent, const char *scopeid, Promise *pp, void *fnptr, const ReportContext *report_context) {

  uint64_t start;
  void *(*ExpandPromise_orig) (enum cfagenttype agent, const char *scopeid, Promise *pp, void *fnptr, const ReportContext *report_context);

  // Get a pointer to the real ExpandPromise() function, to call it later
  ExpandPromise_orig = dlsym(RTLD_NEXT, "ExpandPromise");

  // Get current CPU ticks
  start = rdtsc();

  ExpandPromise_orig(agent, scopeid, pp, fnptr, report_context);

  // Collect information about the execution
  add_bundle_call(pp, rdtsc() - start);

}

// Our version of GenericDeInitialize(): a cleanup function we use to fire the output of statistics
void GenericDeInitialize() {

  void *(*GenericDeInitialize_orig) ();
  
  GenericDeInitialize_orig = dlsym(RTLD_NEXT, "GenericDeInitialize");
  GenericDeInitialize_orig();
  print_stats();
}
