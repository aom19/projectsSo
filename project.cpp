#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <math.h>
#include <string.h>

#define MAX_CAND 10000	/* largest candidate number we will allow */
#define SEQ_SIZE 1000	/* max number of prime factors that a child must test */
#define LINE_LEN 50	/* max characters in a line of data from worker */
#define END_STR "end"	/* message that indicates child is done computation */

typedef struct {
	pid_t pid;
	int fildes[2];
	int sequence;
	} worker_t;

/* 
 * Displays usage information and terminates
 */
void Usage (char *progname) {
	fprintf (stderr, "Usage: %s candidate\n", progname);
	fprintf (stderr, "\tcandidate must be less than %d\n", MAX_CAND);
	 exit(-1);
}

/*
 * Generate a list of primes over the first n integers
 * using the simple Sieve of Eratosthenes method.
 * Returns the number of primes generated.
 */
int Eratosthenes (int n, int **pprimes) {
	int i, j;
	char *sieve;
	int *primes;
	int nonprimes;

	/* create storage for the sieve and initialize to zero */
	sieve = (char *)calloc(n+1, sizeof(char));
	if (sieve == NULL) {
		perror ("calloc of sieve");
		exit(-1);
	}

	/* keep a count of numbers removed from the sieve */
	nonprimes = 1;	/* since 1 is not a prime */

	/* perform the sieve */
	for (i = 2; i <= n; i++) {
		if (!sieve[i]) {
			for (j = 2*i; j <= n; j+=i) {
				if (!sieve[j]) {
					sieve[j] = 1;
					nonprimes++;
				}
			}
		}
	}

	/* create storage for the list of primes */
	*pprimes = primes = (int *)calloc((n - nonprimes + 1), sizeof(int));
	if (primes == NULL) {
		perror ("calloc of primes");
		exit(-1);
	}

	/* now scan through sieve for zero-marked entries */
	i = 0;
	for (j = 2; j <= n; j++) {
		if (!sieve[j]) {
			primes[i++] = j;
		}
	}

	/* be nice and release the allocated memory */
	free ((void *)sieve);

	return (n - nonprimes);
}

/*
 * PrimeFactors evaluates the candidate prime (candidate) 
 * by dividing it by SEQ_SIZE prime factors, as
 * specified by the sequence counter.  Any division with
 * no remainder indicates a prime factor has been found.
 * Returns (by exiting) 0 if all went well.
 */
int PrimeFactors (worker_t *w, unsigned long candidate, int *prime, int nump) {
	int i, p;
	char msg[LINE_LEN];
	int nbytes;

	/* if we are the first child (sequence == 0), delay for 5 seconds */
	if (w->sequence == 0)
		sleep(5);

	for (i = 0; i < SEQ_SIZE; i++) {
		p = w->sequence * SEQ_SIZE + i;

		/* make sure that p does not exceed num_primes */            
		if (p < nump && ! (candidate % prime[p])) {
			sprintf (msg, "%d\n", prime[p]);
			nbytes = write (w->fildes[1], (void *)msg, strlen(msg));
			if (nbytes != strlen(msg)) {
				perror ("write");
				exit(-1);
			}
		}
	}

	/* tell parent that we're finished */
	strcpy (msg, END_STR);
	nbytes = write (w->fildes[1], (void *)msg, strlen(msg));
	if (nbytes != strlen(msg)) {
		perror ("write");
		exit(-1);
	}

	/* close pipe - not really necessary since we're about to exit */
	close (w->fildes[1]);

	/* exit nicely */
	exit(0);
}


int main(int argc, char *argv[]) {
	int *primes;
	int num_primes;		/* the number of primes in [2,range] */
	int num_children;	/* number of worker children */
	int num_active;		/* number of worker children still going */
	worker_t *worker;	/* set of workers (dynamically created) */
	int i, j, k;		/* general loop counters */
	int p;			/* index into prime numbers table */
	int status;		/* status of waitpid() call */
	int nbytes;		/* number of bytes read */
	int range;		/* maximum prime factor to test */
	unsigned long candidate;/* the prime candidate */
	int factors;		/* =1 if any prime factors are found */
	fd_set r;		/* read file descriptor set */
	struct timeval timeout;	/* how long to block on select */
	char buf[LINE_LEN];	/* buffer for data sent over pipe */
	char *str;		/* token returned from buffer */

	/* make sure second argument (candidate) is a valid number */
	if (argc < 2 || argc > 3)
		Usage(argv[0]);

	candidate = atol(argv[1]);
	if (candidate < 2 || candidate > MAX_CAND) 
		Usage(argv[0]);

	/* no need to test prime factors > candidate */
	range = candidate/2;

	/* generate the list of primes up to range */
	num_primes = Eratosthenes(range, &primes);
	printf ("generated %d primes\n", num_primes);

	/*
	 * divide the list of prime factors over a number of children 
	 * such that each child works through a maximum of SEQ_SIZE primes
	 */
	num_children = (int) ceil((double) num_primes/SEQ_SIZE);
	num_active = 0;
	printf ("Dividing the work over %d processes\n", num_children);

	/* generate storage for the process identifiers */
	worker = (worker_t *)calloc(num_children, sizeof(worker_t));
	if (worker == NULL) {
		perror ("calloc of worker");
		exit(-1);
	}

	/* create pipes and fork children to do the work */
	for (i = 0; i < num_children; i++) {
		worker[i].sequence = i;
		pipe(worker[i].fildes);
		worker[i].pid = fork();
		switch (worker[i].pid) {
			case -1:
				perror ("fork");
				exit(1);
				break;
			case 0:
				/* child will only write results */
				close (worker[i].fildes[0]);
				PrimeFactors(&(worker[i]), candidate, 
						primes, num_primes);
				break;
			default:
				/* parent will only read results */
				close (worker[i].fildes[1]);
				num_active++;
		}

	}

	printf ("now displaying any prime factors found\n");
	factors = 0;	/* initialize value */

	/* set timeout to one second */
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;

	/*
 	 * Read results from children.  In order to avoid blocking, 
 	 * we must add the read side of each worker's pipe to the 
	 * read descriptor, then select on these.
	 */
	while (num_active) {
		FD_ZERO(&r);
		for (i = 0; i < num_children; i++) {
			if (worker[i].pid) {
				FD_SET(worker[i].fildes[0], &r);
			}
		}

		/* now check for any workers who have written data */
 		if (select(FD_SETSIZE, &r, NULL, NULL, &timeout) == 0) {
			continue;
		}

		/* which worker has written data? */
		for (i = 0; i < num_children; i++) {
			if (FD_ISSET (worker[i].fildes[0], &r)) {

				/* read the data */
				nbytes = read (worker[i].fildes[0], 
						(void *)buf, 
						(size_t)LINE_LEN);

				/* did read succeed? */
				if (nbytes < 0) {
					perror("read");
					exit(-1);
				}

				/* append the string terminator ('\0') */
				buf[nbytes] = '\0';

				/* tokenize the buffer on the separator */
				str = strtok (buf, "\n");

				while (str) {
					/* is worker done? */
					if (!strcmp (str, END_STR)) {
						close (worker[i].fildes[0]);
						waitpid(worker[i].pid, 
							&status, 0);
						worker[i].pid = 0;
						num_active--;
					}

					/* display result */
					else {
						if (!factors)
							printf ("\n");
						factors++;
						printf ("%s\n", str);
					}

					str = strtok (NULL, "\n");
				}
			}
		}
	}

	if (!factors)
		printf ("\n%lu is prime\n", candidate);

	/* all done */
	return 0;
}