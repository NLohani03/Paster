#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <curl/curl.h>
#include <math.h>
#include <string.h>
#include <stdbool.h> 
#include <arpa/inet.h>
#include "./starter/png_util/crc.h" // starter files provided from school 
#include "./starter/png_util/zutil.h" // starter files provided from school 


#define SERVER_1 "http://ece252-1.uwaterloo.ca:2520/image?img="
#define SERVER_2 "http://ece252-2.uwaterloo.ca:2520/image?img="
#define SERVER_3 "http://ece252-3.uwaterloo.ca:2520/image?img="
#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048576  /* 1024*1024 = 1M */
#define BUF_INC  524288   /* 1024*512  = 0.5M */
#define max(a, b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

typedef unsigned char U8;
typedef unsigned int  U32;

typedef struct chunk {
    U32 length;  /* length of data in the chunk, host byte order */
    U8  type[4]; /* chunk type */
    U8  *p_data; /* pointer to location where the actual data are */
    U32 crc;     /* CRC field  */
} *chunk_p;


typedef struct data_IHDR {// IHDR chunk data 
    U32 width;        /* width in pixels, big endian   */
    U32 height;       /* height in pixels, big endian  */
    U8  bit_depth;    /* num of bits per sample or per palette index.
                         valid values are: 1, 2, 4, 8, 16 */
    U8  color_type;   /* =0: Grayscale; =2: Truecolor; =3 Indexed-color
                         =4: Greyscale with alpha; =6: Truecolor with alpha */
    U8  compression;  /* only method 0 is defined for now */
    U8  filter;       /* only method 0 is defined for now */
    U8  interlace;    /* =0: no interlace; =1: Adam7 interlace */
} *data_IHDR_p;

/* A simple PNG file format, three chunks only*/
typedef struct simple_PNG {
    struct chunk *p_IHDR;
    struct chunk *p_IDAT;  /* only handles one IDAT chunk */  
    struct chunk *p_IEND;
} *simple_PNG_p;


int SERVER_NUM = 0;
pthread_mutex_t mutex;


struct thread_args              /* thread input parameters struct */
{
    char * image_num;
    int * thread_num;
};


typedef struct recv_buf2 {
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */   // this is the fragment number
                     /* <0 indicates an invalid seq number */
} RECV_BUF;


struct recv_buf2 allPNGData[50];    // This holds all the data



void *do_work(void *arg);  /* a routine that can run as a thread by pthreads */
int server_call(int server_num, char * image_num); 
int recv_buf_init(RECV_BUF *ptr, size_t max_size);
int recv_buf_cleanup(RECV_BUF *ptr);


int recv_buf_init(RECV_BUF *ptr, size_t max_size)
{
    void *p = NULL;
    
    if (ptr == NULL) {
        return 1;
    }

    p = malloc(max_size);
    if (p == NULL) {
	return 2;
    }
    
    ptr->buf = p;
    ptr->size = 0;
    ptr->max_size = max_size;
    ptr->seq = -1;              /* valid seq should be non-negative */
    return 0;
}

int recv_buf_cleanup(RECV_BUF *ptr)
{
    if (ptr == NULL) {
	return 1;
    }
    
    free(ptr->buf);
    ptr->size = 0;
    ptr->max_size = 0;
    return 0;
}

size_t header_cb_curl (char *p_recv, size_t size, size_t nmemb, void *userdata) {
    int realSize = size * nmemb;
    RECV_BUF *p = userdata;

    if (realSize > strlen(ECE252_HEADER) && 
    strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {
       
        p->seq = atoi(p_recv + strlen(ECE252_HEADER));

    }
    return realSize;
}

size_t write_cb_curl3 (char *p_recv, size_t size, size_t nmemb, void *p_userdata) { // p_userdata is supplied using curl_easy_setopt(handle, CURLOPT_WRITEDATA, arg)
    size_t realSize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;

    if (p->size + realSize + 1 > p->max_size) {
        size_t new_size = p->max_size + max(BUF_INC, realSize + 1);
        char *q = realloc(p->buf, new_size);
        if (q == NULL) {
            perror("realloc");
            return -1;
        }
        p->buf = q;
        p->max_size = new_size;
    }

    memcpy(p->buf + p->size, p_recv, realSize);
    p->size += realSize;
    p->buf[p->size] = 0;

    return realSize;
}


int server_call(int server_num, char * image_num) {
    // printf("In server_call. SERVER_NUM: %d, image_num: %s\n", server_num, image_num);

    CURL *curl_handle;
    CURLcode res;
    char url[256];
    RECV_BUF recv_buf;
    recv_buf_init(&recv_buf, BUF_SIZE);


    if (server_num == 1) {
        strcpy(url, SERVER_1); 
        strcat(url, image_num);
    } else if (server_num == 2) { 
        strcpy(url, SERVER_2); 
        strcat(url, image_num); 
    } else {
        strcpy(url, SERVER_3); 
        strcat(url, image_num);
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);

    curl_handle = curl_easy_init();
    if(curl_handle) {
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);    // Setting the URL to make request to
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");  // Setting flag for some servers
        curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl);  // function to processes the header (use to get the fragment number)
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl3);   // function to processes the recieved data (use to store the data we get, probably using parts of catpng)
        curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)&recv_buf); // arg are the pointers passed to header_cb_curl function
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&recv_buf);  // arg is the pointers passed to the write_cb_curl3 fucntion
        res = curl_easy_perform(curl_handle);   // Performs the actual request including processing. Will block program until done

        if( res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }
        curl_easy_cleanup(curl_handle); // Destroy and clean up handle
    }

    curl_global_cleanup();  // Clean up library

    // Locking all the threads (so that the threads don't try to update the same position of the array at the same time) and adding to the array holding the data
    pthread_mutex_lock(&mutex);
    if (allPNGData[recv_buf.seq].seq == -1) {
        allPNGData[recv_buf.seq] = recv_buf;
    } else { 
        recv_buf_cleanup(&recv_buf);

    }
    pthread_mutex_unlock(&mutex);

    // Checking if allPNGData array has all entries added
    // returning 0 if the hashmap is not full (all 50 positions have not been set)
    // returning 1 if the hashmap is full (all 50 positions have been set)
    bool gotAllPNGS = true;
    for (int i=0; i<50; i++) {
        if (allPNGData[i].seq == -1) {
            gotAllPNGS = false;
        }
    }
    if (gotAllPNGS == true) {
        return 1;
    } else {
        return 0;
    }
}


void *do_work(void *arg)
{

    //the work that each thread does 
    struct thread_args *p_in = arg;

        while (1) {
            SERVER_NUM++; 
            if (SERVER_NUM == 4) { 
                SERVER_NUM = 1;
            }
            // do one curl call and add image to results and add to hash map
            if (server_call(SERVER_NUM, p_in->image_num) == 1) { // hashmap is full so we need to break from the while loop
                break;
            }
        }
    
    void * return_val = NULL;
    return return_val;
}

void parse_through_buffer() { 
    
    //structs to store the differnt chunks of the final png
    struct chunk finalPNGIHDRChunk;
    struct chunk finalPNGIDATChunk;
    struct chunk finalPNGIENDChunk;


    struct data_IHDR finalIHDRData;
    memset(&finalIHDRData, 0, sizeof(struct data_IHDR)); 

    U32 finalLength = 0;

    // create with malloc an array size pngNum, of simplePngs, which will hold the chunks for each png
    struct simple_PNG* allSimplePNGs = malloc(50*sizeof(struct simple_PNG));
    memset(allSimplePNGs, 0, 50*sizeof(struct simple_PNG));


    U8 * temp1 = NULL;
    unsigned long curr_inflated_data_len = 0; //size of the buffer that holds each inflated data 

    //holds the inflated data of all the PNGS
    U8 *final_inflated_data = NULL;
    int j = 0;
    unsigned long final_increasing_inflated_data_len = 0;  // size of the buffer that is holding the final inflated data 

    U8 header[8];
    for (int i =0; i < 50; i++) { 

        char* buf = allPNGData[i].buf;
        //header
        for (int i=0; i<8; i++) {
            header[i] = 0;
            
        }
        memcpy(header, buf, sizeof(header));

        // move the pointer by n bytes s to point to the next n bytes
        char* next_bytes = buf + sizeof(header);

        // THE IHDR CHUNK
        struct chunk ihdr;

        // ihdr length
        memcpy(&(ihdr.length), next_bytes, sizeof(ihdr.length));
        next_bytes += sizeof(ihdr.length);

        ihdr.length = ntohl(ihdr.length);
        finalLength = ihdr.length;

        // ihdr type
        memcpy(ihdr.type, next_bytes, sizeof(ihdr.type));
        next_bytes += sizeof(ihdr.type);

        finalPNGIHDRChunk.type[0] = ihdr.type[0];
        finalPNGIHDRChunk.type[1] = ihdr.type[1];
        finalPNGIHDRChunk.type[2] = ihdr.type[2];
        finalPNGIHDRChunk.type[3] = ihdr.type[3];


        // ihdr data
        struct data_IHDR ihdrChunk;
        // printf("size: %ld %ld", sizeof(U32), sizeof(ihdrChunk.width));
        memcpy(&(ihdrChunk.width), next_bytes, sizeof(ihdrChunk.width));
        next_bytes += sizeof(ihdrChunk.width);
        // fread(&(ihdrChunk.width), sizeof(U32), 1, fp);
        finalIHDRData.width = ihdrChunk.width;

        memcpy(&(ihdrChunk.height), next_bytes, sizeof(ihdrChunk.height));
        next_bytes += sizeof(ihdrChunk.height);
        // fread(&(ihdrChunk.height), sizeof(U32), 1, fp);
        finalIHDRData.height = htonl(ntohl(ihdrChunk.height)+ntohl(finalIHDRData.height));

        memcpy(&(ihdrChunk.bit_depth), next_bytes, sizeof(ihdrChunk.bit_depth));
        next_bytes += sizeof(ihdrChunk.bit_depth);
        // fread(&(ihdrChunk.bit_depth), sizeof(U8), 1, fp);
        finalIHDRData.bit_depth = ihdrChunk.bit_depth;

        memcpy(&(ihdrChunk.color_type), next_bytes, sizeof(ihdrChunk.color_type));
        next_bytes += sizeof(ihdrChunk.color_type);
        // fread(&(ihdrChunk.color_type), sizeof(U8), 1, fp);
        finalIHDRData.color_type = ihdrChunk.color_type;


        memcpy(&(ihdrChunk.compression), next_bytes, sizeof(ihdrChunk.compression));
        next_bytes += sizeof(ihdrChunk.compression);
        // fread(&(ihdrChunk.compression), sizeof(U8), 1, fp);
        finalIHDRData.compression = ihdrChunk.compression;

        memcpy(&(ihdrChunk.filter), next_bytes, sizeof(ihdrChunk.filter));
        next_bytes += sizeof(ihdrChunk.filter);
        // fread(&(ihdrChunk.filter), sizeof(U8), 1, fp);
        finalIHDRData.filter = ihdrChunk.filter;

        memcpy(&(ihdrChunk.interlace), next_bytes, sizeof(ihdrChunk.interlace));
        next_bytes += sizeof(ihdrChunk.interlace);
        // fread(&(ihdrChunk.interlace), sizeof(U8), 1, fp);
        finalIHDRData.interlace = ihdrChunk.interlace;


        ihdr.p_data = (U8*)&ihdrChunk;

        //idhr crc
        memcpy(&(ihdr.crc), next_bytes, sizeof(ihdr.crc));
        next_bytes += sizeof(ihdr.crc);
        ihdr.crc = ntohl(ihdr.crc);

        
         // THE IDAT CHUNK
        struct chunk idat;

        // idat length
        memcpy(&(idat.length), next_bytes, sizeof(idat.length));
        next_bytes += sizeof(idat.length);
        idat.length = ntohl(idat.length);

        // idat type
        memcpy(idat.type, next_bytes, sizeof(idat.type));
        next_bytes += sizeof(idat.type);
        finalPNGIDATChunk.type[0] = idat.type[0];
        finalPNGIDATChunk.type[1] = idat.type[1];
        finalPNGIDATChunk.type[2] = idat.type[2];
        finalPNGIDATChunk.type[3] = idat.type[3];

        // idat data
        idat.p_data = malloc(idat.length * sizeof(U8));
        memcpy(idat.p_data, next_bytes, idat.length);
        next_bytes += idat.length;
       

        // idat crc
        memcpy(&(idat.crc), next_bytes, sizeof(idat.crc));
        next_bytes += sizeof(idat.crc);
        idat.crc = ntohl(idat.crc);
        
        
        // THE IEND CHUNK
        struct chunk iend;

        // iend length
        memcpy(&(iend.length), next_bytes, sizeof(iend.length));
        next_bytes += sizeof(iend.length);
        iend.length = ntohl(iend.length);
        finalPNGIENDChunk.length = iend.length;

        // iend type
        memcpy(iend.type, next_bytes, sizeof(iend.type));
        next_bytes += sizeof(iend.type);
        finalPNGIENDChunk.type[0] = iend.type[0];
        finalPNGIENDChunk.type[1] = iend.type[1];
        finalPNGIENDChunk.type[2] = iend.type[2];
        finalPNGIENDChunk.type[3] = iend.type[3];

        // iend data
        iend.p_data = malloc(iend.length * sizeof(U8));
        memcpy(iend.p_data, next_bytes, iend.length);
        next_bytes += iend.length;

        // iend crc
        memcpy(&(iend.crc), next_bytes, sizeof(iend.crc));

        // Putting the png chunks in a simple_png struct and add that struct to the allSimplePNGs array
        struct simple_PNG newSimplePNG;
        newSimplePNG.p_IHDR = &ihdr;
        newSimplePNG.p_IDAT = &idat;
        newSimplePNG.p_IEND = &iend;
        allSimplePNGs[i] = newSimplePNG;


        //now that we have all the chunks from the each png, we can start combining them
        final_increasing_inflated_data_len = (finalIHDRData.height*(finalIHDRData.width*4+1));
        curr_inflated_data_len = ihdrChunk.height * (ihdrChunk.width * 4 + 1);
       
        //to reallocate more space for the final inflated data as PNGs increase
        temp1 = (U8 *)realloc(final_inflated_data, final_increasing_inflated_data_len * sizeof(U8));
        final_inflated_data = temp1;

        U8 *curr_inflated_data = (U8 *)malloc(curr_inflated_data_len * sizeof(U8));     // buffer that has the inflated data value of each PNG
        int ret = mem_inf(curr_inflated_data, &curr_inflated_data_len, idat.p_data, idat.length);
        if (ret == 0) { /* success */

        } else { /* failure */
            fprintf(stderr,"mem_inf failed. ret = %d.\n", ret);
        }
        
        for (int i = 0; i < curr_inflated_data_len; i++) {
            final_inflated_data[j] = curr_inflated_data[i]; 
            j++;
        }


        free(idat.p_data);
        free(iend.p_data);
        free(curr_inflated_data);
    }

    //time to deflate the data 
    U64 final_inflated_data_len = 0;      // final inflated  value's length
    final_inflated_data_len = ntohl(finalIHDRData.height)*(ntohl(finalIHDRData.width)*4+1);

    //buffer that has the deflated values of all PNGs
    U8 *final_deflated_value = (U8*) malloc(final_inflated_data_len*sizeof(U8));
    U64 len_def = compressBound((ntohl(finalIHDRData.height)*(ntohl(finalIHDRData.width)*4+1)));


    int ret = mem_def(final_deflated_value, &len_def, final_inflated_data, final_inflated_data_len, Z_DEFAULT_COMPRESSION);
    if (ret == 0) { /* success */

    } else { /* failure */
        fprintf(stderr,"mem_def failed. ret = %d.\n", ret);
    }

    //putting all this together
    finalPNGIHDRChunk.length = finalLength;
    finalPNGIDATChunk.length = len_def;


    finalPNGIHDRChunk.p_data = (U8*)&finalIHDRData;
    finalPNGIDATChunk.p_data = final_deflated_value;
    finalPNGIENDChunk.p_data = NULL;


    unsigned char *ihdrCrcArray = malloc(4 + finalPNGIHDRChunk.length + 1); 
    memset(ihdrCrcArray, 0, 4 + finalPNGIHDRChunk.length + 1);
    for (int i=0; i<(4+finalPNGIHDRChunk.length); i++) {
        if (i<4) {
            ihdrCrcArray[i] = finalPNGIHDRChunk.type[i];
        } else {
            ihdrCrcArray[i] = finalPNGIHDRChunk.p_data[i-4];
        }
    }

    unsigned char *idatCrcArray = malloc(4 + finalPNGIDATChunk.length + 1);  // We are putting ihdr.type and ihdr.p_data inside the crcArray. ihdr.type is 4 byte size and ihdr.p_data is the size of ihdr.length. We are adding 1 also for the crcArray to make the last character null
    memset(idatCrcArray, 0, sizeof(finalPNGIDATChunk.type) + finalPNGIDATChunk.length + 1);
    for (int i=0; i<(sizeof(finalPNGIDATChunk.type)+finalPNGIDATChunk.length); i++) {
        if (i<4) {
            idatCrcArray[i] = finalPNGIDATChunk.type[i];
        } else {
            idatCrcArray[i] = final_deflated_value[i-4];
        }
    }

    unsigned char *iendcrcArray = malloc(4 + finalPNGIENDChunk.length + 1);  // We are putting ihdr.type and ihdr.p_data inside the crcArray. ihdr.type is 4 byte size and ihdr.p_data is the size of ihdr.length. We are adding 1 also for the crcArray to make the last character null
    memset(iendcrcArray, 0, sizeof(finalPNGIENDChunk.type) + finalPNGIENDChunk.length + 1);
    for (int i=0; i<(sizeof(finalPNGIENDChunk.type)+finalPNGIENDChunk.length); i++) {
        if (i<4) {
            iendcrcArray[i] = finalPNGIENDChunk.type[i];
        } else {
            iendcrcArray[i] = finalPNGIENDChunk.p_data[i-sizeof(finalPNGIENDChunk.type)];
        }
    }

    unsigned long ihdrCrcFunctionValue = crc(ihdrCrcArray, finalPNGIHDRChunk.length+4);
    unsigned long idatCrcFunctionValue = crc(idatCrcArray, finalPNGIDATChunk.length+4);
    unsigned long iendCrcFunctionValue = crc(iendcrcArray, finalPNGIENDChunk.length+4);

    finalPNGIHDRChunk.crc = ihdrCrcFunctionValue;
    finalPNGIDATChunk.crc = idatCrcFunctionValue;
    finalPNGIENDChunk.crc = iendCrcFunctionValue;

    // Now create a new png
    struct simple_PNG finalPNG;
    finalPNG.p_IHDR = &finalPNGIHDRChunk;
    finalPNG.p_IDAT = &finalPNGIDATChunk;
    finalPNG.p_IEND = &finalPNGIENDChunk;

    // Create file
    FILE * concatenatedPNG = fopen("./all.png", "w+");
    if (concatenatedPNG == NULL) {
        //do nothing - error
        exit(1);
    }

    fwrite(header, 1, 8 , concatenatedPNG);

    //IHDR chunk
    finalPNG.p_IHDR->length = htonl(finalPNG.p_IHDR->length);
    fwrite(&finalPNG.p_IHDR->length, 1, sizeof(finalPNG.p_IHDR->length), concatenatedPNG);
    finalPNG.p_IHDR->length = ntohl(finalPNG.p_IHDR->length);
    fwrite(finalPNG.p_IHDR->type, 1, sizeof(finalPNG.p_IHDR->type), concatenatedPNG);
    fwrite(finalPNG.p_IHDR->p_data, sizeof(U8), finalPNG.p_IHDR->length, concatenatedPNG);
    finalPNG.p_IHDR->crc = htonl(finalPNG.p_IHDR->crc);
    fwrite(&finalPNG.p_IHDR->crc, 1, sizeof(finalPNG.p_IHDR->crc), concatenatedPNG);


    //IDAT chunk 
    finalPNG.p_IDAT->length = htonl(finalPNG.p_IDAT->length);
    fwrite(&finalPNG.p_IDAT->length, 1, sizeof(finalPNG.p_IDAT->length), concatenatedPNG);
    finalPNG.p_IDAT->length = ntohl(finalPNG.p_IDAT->length); 
    fwrite(finalPNG.p_IDAT->type, 1, sizeof(finalPNG.p_IDAT->type), concatenatedPNG);
    fwrite(finalPNG.p_IDAT->p_data, sizeof(U8), finalPNG.p_IDAT->length, concatenatedPNG);
    finalPNG.p_IDAT->crc = htonl(finalPNG.p_IDAT->crc);
    fwrite(&finalPNG.p_IDAT->crc, 1, sizeof(finalPNG.p_IDAT->crc), concatenatedPNG);
    
    //IDEND chunk
    finalPNG.p_IEND->length = htonl(finalPNG.p_IEND->length);
    fwrite(&finalPNG.p_IEND->length, 1, sizeof(finalPNG.p_IEND->length), concatenatedPNG);
    finalPNG.p_IEND->length = ntohl(finalPNG.p_IEND->length);
    fwrite(finalPNG.p_IEND->type, 1, sizeof(finalPNG.p_IEND->type), concatenatedPNG);
    fwrite(finalPNG.p_IEND->p_data, sizeof(U8), finalPNG.p_IEND->length, concatenatedPNG);
    finalPNG.p_IEND->crc = htonl(finalPNG.p_IEND->crc);
    fwrite(&finalPNG.p_IEND->crc, 1, sizeof(finalPNG.p_IEND->crc), concatenatedPNG);

    fclose(concatenatedPNG);
    

    free(temp1);
    free(ihdrCrcArray);
    free(idatCrcArray);
    free(iendcrcArray);
    free(final_deflated_value);
    free(allSimplePNGs);

}

int main(int argc, char **argv)
{
    for (int i = 0; i < 50; i++) {
        allPNGData[i].seq = -1; // setting all fragment numbers to be -1 to indicate that that fragment has not been recieved yet
    }

    int c;
    int t = 1;
    int n = 1;
    char * IMAGE_NUM;
    const char * NUM_THREADS;
    char *str = "option requires an argument";
    
    while ((c = getopt (argc, argv, "t:n:")) != -1) {
        switch (c) {
            case 't':
                NUM_THREADS = optarg;
                t = strtoul(optarg, NULL, 10);
                if (t <= 0) {
                    fprintf(stderr, "%s: %s > 0 -- 't'\n", argv[0], str);
                    return -1;
                }
                break;
            case 'n':
                IMAGE_NUM = optarg;
                n = strtoul(optarg, NULL, 10);
                if (n <= 0 || n > 3) {
                    fprintf(stderr, "%s: %s 1, 2, or 3 -- 'n'\n", argv[0], str);
                    return -1;
                }
                break;
            default:
                return -1;
        }
    }

    pthread_t *p_tids = malloc(sizeof(pthread_t) * atoi(NUM_THREADS));
    struct thread_args in_params[atoi(NUM_THREADS)];
    pthread_mutex_init(&mutex, NULL);
    
    for (int i=0; i<atoi(NUM_THREADS); i++) {
        in_params[i].image_num = IMAGE_NUM;
        in_params[i].thread_num = &i;
        pthread_create(p_tids + i, NULL, do_work, in_params + i); 
    }

    for (int i=0; i<atoi(NUM_THREADS); i++) {
        pthread_join(p_tids[i], NULL);
    }

    
    /* cleaning up */
    pthread_mutex_destroy(&mutex);
    free(p_tids);   

    parse_through_buffer();

    for (int i= 0; i<50; i++) {
        recv_buf_cleanup(&allPNGData[i]);

    }

    return 0;
}
