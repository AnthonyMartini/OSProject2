#include <dirent.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <time.h>
#include <pthread.h>

#define BUFFER_SIZE 1048576 // 1MB
#define NUM_THREADS 20

int cmp(const void *a, const void *b)
{
	return strcmp(*(char **)a, *(char **)b);
}

int n = 0;
int nfiles = 0;
char base_path[100];
// create a single zipped package with all PPM files in lexicographical order
int total_in = 0, total_out = 0;
FILE *f_out;
pthread_mutex_t n_mutex;
char **files = NULL;

typedef struct
{
	int index;
	int size;			 // Size of the compressed data
	unsigned char *data; // Pointer to the compressed data
} CompressedFile;
CompressedFile *compressed_files;

void *process_file(void *arg)
{
	int thread_num = *((int *)arg);
	int local_n;

	while (1)
	{
		pthread_mutex_lock(&n_mutex); // Lock to get the next file to process
		if (n >= nfiles)
		{
			pthread_mutex_unlock(&n_mutex);
			break; // Exit the loop if all files are processed
		}

		local_n = n++;					// Get the next index to process, and increment n
		pthread_mutex_unlock(&n_mutex); // Release the lock

		// Get the full path of the file
		int len = strlen(base_path) + strlen(files[local_n]) + 2;
		char *full_path = malloc(len * sizeof(char));
		assert(full_path != NULL);
		strcpy(full_path, base_path);
		strcat(full_path, files[local_n]);

		unsigned char buffer_in[BUFFER_SIZE];
		unsigned char buffer_out[BUFFER_SIZE];

		// Load the file into memory
		FILE *f_in = fopen(full_path, "r");
		assert(f_in != NULL);
		int nbytes = fread(buffer_in, sizeof(unsigned char), BUFFER_SIZE, f_in);
		fclose(f_in);
		total_in += nbytes;

		// Compress the file data using zlib
		z_stream strm;
		int ret = deflateInit(&strm, 9); // Highest compression level
		assert(ret == Z_OK);
		strm.avail_in = nbytes;
		strm.next_in = buffer_in;
		strm.avail_out = BUFFER_SIZE;
		strm.next_out = buffer_out;

		ret = deflate(&strm, Z_FINISH); // Finalize compression
		assert(ret == Z_STREAM_END);
		deflateEnd(&strm);

		// Calculate the number of zipped bytes
		int nbytes_zipped = BUFFER_SIZE - strm.avail_out;

		// Store compressed data in the shared array
		pthread_mutex_lock(&n_mutex); // Lock to safely update the array
		compressed_files[local_n].size = nbytes_zipped;
		compressed_files[local_n].index = local_n;
		compressed_files[local_n].data = malloc(nbytes_zipped * sizeof(unsigned char));
		memcpy(compressed_files[local_n].data, buffer_out, nbytes_zipped);
		pthread_mutex_unlock(&n_mutex); // Release the lock

		free(full_path); // Free allocated memory for file path
	}

	return NULL;
}

// Comparator function to sort compressed files by index (matching original order)
int compare_compressed_files(const void *a, const void *b)
{
	return ((CompressedFile *)a)->index - ((CompressedFile *)b)->index;
}

int main(int argc, char **argv)
{
	pthread_mutex_init(&n_mutex, NULL);
	struct timespec start, end;
	clock_gettime(CLOCK_MONOTONIC, &start);

	assert(argc == 2);

	DIR *d;
	struct dirent *dir;

	d = opendir(argv[1]);
	if (d == NULL)
	{
		printf("An error has occurred\n");
		return 0;
	}

	// Create sorted list of PPM files
	while ((dir = readdir(d)) != NULL)
	{
		files = realloc(files, (nfiles + 1) * sizeof(char *));
		assert(files != NULL);

		int len = strlen(dir->d_name);
		if (dir->d_name[len - 4] == '.' && dir->d_name[len - 3] == 'p' && dir->d_name[len - 2] == 'p' && dir->d_name[len - 1] == 'm')
		{
			files[nfiles] = strdup(dir->d_name);
			assert(files[nfiles] != NULL);
			nfiles++;
		}
	}
	closedir(d);
	qsort(files, nfiles, sizeof(char *), cmp);

	f_out = fopen("video.vzip", "w");
	assert(f_out != NULL);

	compressed_files = malloc(nfiles * sizeof(CompressedFile));

	pthread_t threads[NUM_THREADS];
	int thread_args[NUM_THREADS];

	strcpy(base_path, argv[1]);
	strcat(base_path, "/");

	// Create threads to process files
	for (int i = 0; i < NUM_THREADS; i++)
	{
		thread_args[i] = i;
		pthread_create(&threads[i], NULL, process_file, &thread_args[i]);
	}

	// Wait for all threads to finish
	for (int i = 0; i < NUM_THREADS; i++)
	{
		pthread_join(threads[i], NULL);
	}

	// Sort compressed files by index (this ensures lexicographical order)
	qsort(compressed_files, nfiles, sizeof(CompressedFile), compare_compressed_files);

	// Write the compressed data to the output file in order
	for (int i = 0; i < nfiles; i++)
	{
		fwrite(&compressed_files[i].size, sizeof(int), 1, f_out);
		fwrite(compressed_files[i].data, sizeof(unsigned char), compressed_files[i].size, f_out);
		total_out += compressed_files[i].size;

		free(compressed_files[i].data); // Free allocated memory for data
	}
	fclose(f_out);

	printf("Compression rate: %.2lf%%\n", 100.0 * (total_in - total_out) / total_in);

	for (int i = 0; i < nfiles; i++)
		free(files[i]);
	free(files);

	clock_gettime(CLOCK_MONOTONIC, &end);
	printf("Time: %.2f seconds\n", ((double)end.tv_sec + 1.0e-9 * end.tv_nsec) - ((double)start.tv_sec + 1.0e-9 * start.tv_nsec));

	return 0;
}
