/* 
 * Operating Systems  (2INC0)  Practical Assignment.
 * Condition Variables Application.
 *
 * Beau Pullens (2099993)
 * Jingxuan Zhang (2088746)
 * Anna Schrasser (2069237)
 *
 * Grading:
 * Students who hand in clean code that fully satisfies the minimum requirements will get an 8. 
 * Extra steps can lead to higher marks because we want students to take the initiative.
 */
 
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>  
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include "prodcons.h"

static ITEM buffer[BUFFER_SIZE];

static void rsleep (int t);	    // already implemented (see below)
static ITEM get_next_item (void);   // already implemented (see below)

static pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER; // mutex ensures only one thread can access the buffer
static pthread_cond_t buffer_not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t has_next_item[NROF_PRODUCERS] = {[0 ... NROF_PRODUCERS - 1] = PTHREAD_COND_INITIALIZER};

static int input = 0; // next index to place item in buffer
static int output = 0; // next index to take item from buffer
static int count = 0; // current number of items in buffer
static ITEM next_expected = 0; // next item that needs to be put into buffer to maintain ascending order
static int producer_items[NROF_PRODUCERS]; // keeps track of what item every producer has

static int num_broadcasts = 0;
static int num_signals = 0;


/* producer thread */
static void * 
producer (void * arg)
{
	int id = *(int *)arg;

	// set the item to -1 to avoid the loop immediately terminating
	ITEM item = -1;

    while (item != NROF_ITEMS)
    {
        // TODO: 
        	// * get the new item
			// * put the item into buffer[]
		
		// get the next item and break if all items are given already
		item = get_next_item();
		if (item == NROF_ITEMS) {
			break;
		}

        rsleep (100);	// simulating all kind of activities...

		// lock the mutex
		pthread_mutex_lock(&buffer_mutex);

		// let all other producers know the current item
		producer_items[id] = item;
		
		// if buffer is full or this producer does not have the next item then wait
		while (count == BUFFER_SIZE || item != next_expected) {
			pthread_cond_wait(&has_next_item[id], &buffer_mutex);
		}

		// if buffer was empty before, do not signal consumer
		bool was_empty = (count == 0);

		// put an item into the buffer
       	buffer[input] = item;
		producer_items[id] = -1; // set back to empty
		input = (input + 1) % BUFFER_SIZE;
		count++;
		next_expected++;

		// signal to the consumer that the buffer is not empty if buffer was empty before
		if (was_empty) {
			pthread_cond_signal(&buffer_not_empty);
			num_signals++;
		}

		// signal to the producer that has the next item, but only if the buffer is not full
		if (count < BUFFER_SIZE) {
			for (int i = 0; i < NROF_PRODUCERS; i++) {
				if (producer_items[i] == next_expected) {
					pthread_cond_signal(&has_next_item[i]);
					num_signals++;
					break;
				}
			}
		}

		// release the mutex
        pthread_mutex_unlock(&buffer_mutex);
    }
	return (NULL);
}

/* consumer thread */
static void * 
consumer (void * arg)
{
	// counter of how many items have been processed
	int items_consumed = 0;

    while (items_consumed < NROF_ITEMS)
    {
        // TODO: 
	      // * get the next item from buffer[]
	      // * print the number to stdout

		// lock the mutex
		pthread_mutex_lock(&buffer_mutex);

		// if buffer is empty then wait
		while (count == 0) {
			pthread_cond_wait(&buffer_not_empty, &buffer_mutex);
		}

		// only signal producers if the buffer was full before, since otherwise they were not waiting
		bool was_full = (count == BUFFER_SIZE);

		// take the next item
		ITEM item = buffer[output];
		output = (output + 1) % BUFFER_SIZE;
		count--;
		items_consumed++;

		// signal producer that has next expected item that buffer is no longer full but only if the buffer was full
		if (was_full) {
			for (int i = 0; i < NROF_PRODUCERS; i++) {
				if (producer_items[i] == next_expected) {
					pthread_cond_signal(&has_next_item[i]);
					num_signals++;
					break;
				}
			}
		}

		// release the mutex
		pthread_mutex_unlock(&buffer_mutex);
		
		// process the item
		printf("%d\n", item);
        rsleep (100);		// simulating all kind of activities...
    }
	return (NULL);
}

int main (void)
{
	// create producer threads
    pthread_t producer_threads[NROF_PRODUCERS];
	int ids[NROF_PRODUCERS];
    for (int i = 0; i < NROF_PRODUCERS; i++) {
		ids[i] = i;
		producer_items[i] = -1;
        pthread_create(&producer_threads[i], NULL, producer, &ids[i]);
    }

	// create consumer thread
	pthread_t consumer_thread;
    pthread_create(&consumer_thread, NULL, consumer, NULL);

	// wait for producer threads to finish first, because consumer can only finish after all producers finished producing items
    for (int i = 0; i < NROF_PRODUCERS; i++) {
        pthread_join(producer_threads[i], NULL);
    }

	// wait for consumer thread to finish
    pthread_join(consumer_thread, NULL);

	// print the number of broadcasts and signals
	fprintf(stderr, "Number of broadcasts: %d\nNumber of signals: %d\n", num_broadcasts, num_signals);

    return 0;
}

/*
 * rsleep(int t)
 *
 * The calling thread will be suspended for a random amount of time between 0 and t microseconds
 * At the first call, the random generator is seeded with the current time
 */
static void 
rsleep (int t)
{
    static bool first_call = true;
    
    if (first_call == true)
    {
        srandom (time(NULL));
        first_call = false;
    }
    usleep (random () % t);
}


/* 
 * get_next_item()
 *
 * description:
 *	thread-safe function to get a next job to be executed
 *	subsequent calls of get_next_item() yields the values 0..NROF_ITEMS-1 
 *	in arbitrary order 
 *	return value NROF_ITEMS indicates that all jobs have already been given
 * 
 * parameters:
 *	none
 *
 * return value:
 *	0..NROF_ITEMS-1: job number to be executed
 *	NROF_ITEMS:	 ready
 */
static ITEM
get_next_item(void)
{
    static pthread_mutex_t  job_mutex   = PTHREAD_MUTEX_INITIALIZER;
    static bool    jobs[NROF_ITEMS+1] = { false }; // keep track of issued jobs
    static int     counter = 0;    // seq.nr. of job to be handled
    ITEM           found;          // item to be returned
	
	/* avoid deadlock: when all producers are busy but none has the next expected item for the consumer 
	 * so requirement for get_next_item: when giving the (i+n)'th item, make sure that item (i) is going to be handled (with n=nrof-producers)
	 */
	pthread_mutex_lock (&job_mutex);

	counter++;
	if (counter > NROF_ITEMS)
	{
	    // we're ready
	    found = NROF_ITEMS;
	}
	else
	{
	    if (counter < NROF_PRODUCERS)
	    {
	        // for the first n-1 items: any job can be given
	        // e.g. "random() % NROF_ITEMS", but here we bias the lower items
	        found = (random() % (2*NROF_PRODUCERS)) % NROF_ITEMS;
	    }
	    else
	    {
	        // deadlock-avoidance: item 'counter - NROF_PRODUCERS' must be given now
	        found = counter - NROF_PRODUCERS;
	        if (jobs[found] == true)
	        {
	            // already handled, find a random one, with a bias for lower items
	            found = (counter + (random() % NROF_PRODUCERS)) % NROF_ITEMS;
	        }    
	    }
	    
	    // check if 'found' is really an unhandled item; 
	    // if not: find another one
	    if (jobs[found] == true)
	    {
	        // already handled, do linear search for the oldest
	        found = 0;
	        while (jobs[found] == true)
            {
                found++;
            }
	    }
	}
    	jobs[found] = true;
			
	pthread_mutex_unlock (&job_mutex);
	return (found);
}



