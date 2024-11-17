#include <dirent.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <time.h>
#include <pthread.h>

// Constants for program configuration
#define BUFFER_SIZE (1024 * 1024)  // 1MB buffer size for reading files
#define NUM_THREADS 20             // Number of threads for parallel processing

// Global variables for file handling
char inputDirectory[100];          // Base directory path
char **ppmFiles = NULL;           // Array to store PPM filenames
int totalFiles = 0;               // Total number of PPM files found
int currentFileIndex = 0;         // Index of current file being processed

// Statistics for compression
int totalBytesInput = 0;          // Total bytes before compression
int totalBytesOutput = 0;         // Total bytes after compression

// Thread synchronization
pthread_mutex_t fileMutex;        // Mutex for thread-safe file processing
FILE *outputFile;                 // Output file handle

// Structure to store compressed file data
typedef struct {
    int fileIndex;                // Original position in file list
    int compressedSize;           // Size after compression
    unsigned char *compressedData; // Compressed file contents
} CompressedFile;

CompressedFile *compressedFiles;  // Array to store all compressed files

// Function to compare strings for sorting filenames
int compareStrings(const void *a, const void *b) {
    return strcmp(*(char **)a, *(char **)b);
}

// Function to compare compressed files by index
int compareCompressedFiles(const void *a, const void *b) {
    CompressedFile *file1 = (CompressedFile *)a;
    CompressedFile *file2 = (CompressedFile *)b;
    return file1->fileIndex - file2->fileIndex;
}

// Function to compress a single PPM file
void *compressPpmFile(void *threadId) {
    int localFileIndex;
    unsigned char inputBuffer[BUFFER_SIZE];
    unsigned char outputBuffer[BUFFER_SIZE];
    
    // Keep processing files until none are left
    while (1) {
        // Safely get next file to process
        pthread_mutex_lock(&fileMutex);
        if (currentFileIndex >= totalFiles) {
            pthread_mutex_unlock(&fileMutex);
            break;
        }
        localFileIndex = currentFileIndex++;
        pthread_mutex_unlock(&fileMutex);
        
        // Create full file path
        char *fullPath = malloc(strlen(inputDirectory) + strlen(ppmFiles[localFileIndex]) + 2);
        assert(fullPath != NULL);
        sprintf(fullPath, "%s/%s", inputDirectory, ppmFiles[localFileIndex]);
        
        // Read the PPM file
        FILE *inputFile = fopen(fullPath, "r");
        assert(inputFile != NULL);
        int bytesRead = fread(inputBuffer, 1, BUFFER_SIZE, inputFile);
        fclose(inputFile);
        
        pthread_mutex_lock(&fileMutex);
        totalBytesInput += bytesRead;
        pthread_mutex_unlock(&fileMutex);
        
        // Set up zlib compression
        z_stream compressionStream;
        compressionStream.zalloc = Z_NULL;
        compressionStream.zfree = Z_NULL;
        compressionStream.opaque = Z_NULL;
        
        // Initialize compression
        int result = deflateInit(&compressionStream, 9);  // Maximum compression
        assert(result == Z_OK);
        
        // Prepare compression buffers
        compressionStream.avail_in = bytesRead;
        compressionStream.next_in = inputBuffer;
        compressionStream.avail_out = BUFFER_SIZE;
        compressionStream.next_out = outputBuffer;
        
        // Perform compression
        result = deflate(&compressionStream, Z_FINISH);
        assert(result == Z_STREAM_END);
        deflateEnd(&compressionStream);
        
        // Calculate compressed size
        int compressedSize = BUFFER_SIZE - compressionStream.avail_out;
        
        // Store compressed data
        pthread_mutex_lock(&fileMutex);
        compressedFiles[localFileIndex].fileIndex = localFileIndex;
        compressedFiles[localFileIndex].compressedSize = compressedSize;
        compressedFiles[localFileIndex].compressedData = malloc(compressedSize);
        memcpy(compressedFiles[localFileIndex].compressedData, outputBuffer, compressedSize);
        pthread_mutex_unlock(&fileMutex);
        
        free(fullPath);
    }
    
    return NULL;
}

int main(int argc, char **argv) {
    // Check command line arguments
    if (argc != 2) {
        printf("Usage: %s <directory_path>\n", argv[0]);
        return 1;
    }
    
    // Initialize program
    pthread_mutex_init(&fileMutex, NULL);
    struct timespec startTime, endTime;
    clock_gettime(CLOCK_MONOTONIC, &startTime);
    
    // Open input directory
    DIR *dir = opendir(argv[1]);
    if (dir == NULL) {
        printf("Error: Could not open directory %s\n", argv[1]);
        return 1;
    }
    
    // Read and store all PPM files
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        int nameLength = strlen(entry->d_name);
        
        // Check if file is a PPM
        if (nameLength > 4 && strcmp(entry->d_name + nameLength - 4, ".ppm") == 0) {
            ppmFiles = realloc(ppmFiles, (totalFiles + 1) * sizeof(char *));
            assert(ppmFiles != NULL);
            ppmFiles[totalFiles] = strdup(entry->d_name);
            assert(ppmFiles[totalFiles] != NULL);
            totalFiles++;
        }
    }
    closedir(dir);
    
    // Sort filenames
    qsort(ppmFiles, totalFiles, sizeof(char *), compareStrings);
    
    // Create output file
    outputFile = fopen("video.vzip", "w");
    assert(outputFile != NULL);
    
    // Allocate space for compressed files
    compressedFiles = malloc(totalFiles * sizeof(CompressedFile));
    assert(compressedFiles != NULL);
    
    // Store input directory path
    strcpy(inputDirectory, argv[1]);
    
    // Create and start compression threads
    pthread_t threads[NUM_THREADS];
    int threadIds[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        threadIds[i] = i;
        pthread_create(&threads[i], NULL, compressPpmFile, &threadIds[i]);
    }
    
    // Wait for all threads to finish
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Sort compressed files to maintain original order
    qsort(compressedFiles, totalFiles, sizeof(CompressedFile), compareCompressedFiles);
    
    // Write compressed data to output file
    for (int i = 0; i < totalFiles; i++) {
        fwrite(&compressedFiles[i].compressedSize, sizeof(int), 1, outputFile);
        fwrite(compressedFiles[i].compressedData, 1, compressedFiles[i].compressedSize, outputFile);
        totalBytesOutput += compressedFiles[i].compressedSize;
        free(compressedFiles[i].compressedData);
    }
    
    // Calculate and display statistics
    double compressionRate = 100.0 * (totalBytesInput - totalBytesOutput) / totalBytesInput;
    printf("Compression rate: %.2f%%\n", compressionRate);
    
    // Calculate execution time
    clock_gettime(CLOCK_MONOTONIC, &endTime);
    double executionTime = ((double)endTime.tv_sec + 1.0e-9 * endTime.tv_nsec) - 
                          ((double)startTime.tv_sec + 1.0e-9 * startTime.tv_nsec);
    printf("Execution time: %.2f seconds\n", executionTime);
    
    // Clean up
    for (int i = 0; i < totalFiles; i++) {
        free(ppmFiles[i]);
    }
    free(ppmFiles);
    free(compressedFiles);
    fclose(outputFile);
    
    return 0;
}