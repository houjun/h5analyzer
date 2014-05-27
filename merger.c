#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "utlist.h"

#define MAX_DIM                 8
#define OP_NAME_LEN             64
#define FILE_PATH_LEN           128
#define LINE_MAX_LEN            256 
#define MAX_FILE_PER_LOG        256 
#define MAX_DATASET_PER_LOG     256 
#define MAX_SELECTION_PER_LOG   2048

int cur_fileid;
int cur_datasetid;                      // current datasetID opened by H5Dopen* for current log
int cur_selectionid;                    // current selectionID for current log

int validselect;                        // H5Sselect_* is parsed only when previous H5Dget_space is 
                                        // valid (when it is reading)
int validgetspace;                                        


typedef struct file_info {
    int         valid;
    char        filename[FILE_PATH_LEN];
    uint64_t    file_id;
} File_info;

typedef struct dataset_info {
    // hid_t H5Dopen1( hid_t loc_id, const char *name )
    // hid_t H5Dopen2( hid_t loc_id, const char *name, hid_t dapl_id )
    // -Returns a dataset identifier if successful

    int         valid;                  // only valid after H5Dopen()
                                        // invilidate after H5Dclose()

    uint64_t    dataset_id;             // KEY: return value of H5DOpen*() 
                                        //      also matches parameter of H5Dget_space
    char        open_op[OP_NAME_LEN];
    uint64_t    loc_id;
    char        name[FILE_PATH_LEN];    // including /path/to/file/
    uint64_t    dapl_id;                // only used for H5Dopen2
    double      open_time;


} Dataset_info;

// each H5Dget_space* will create a Selection_info struct
typedef struct selection_info {
    
    //Dataset_info*  datasetinfo_ptr;

    char        name[FILE_PATH_LEN];    // including /path/to/file/

    // KEY: return value of H5Dget_space() & parameter of H5Sselect_*
    uint64_t    space_id;


    // H5Sselect_*
    double      walltime;               // actually no use, see read_time
    char        op[OP_NAME_LEN];        // TODO: only support H5S_SELECT_SET now
    char        select_type;            // H for hyperslab, E for elements

    // H5Sselect_hyperslab
    int         dim;
    uint64_t    start[MAX_DIM];
    uint64_t    stride[MAX_DIM];
    uint64_t    count[MAX_DIM];
    uint64_t    block[MAX_DIM];

    // H5Sselect_elements
    uint64_t    num_elements;
    uint64_t*   coord;                  // don't forget to free this

} Selection_info;


typedef struct h5dread {

    double      walltime;
    uint64_t    dataset_id;
    char        mem_type_id[OP_NAME_LEN];
    uint64_t    mem_space_id;
    uint64_t    file_space_id;
    uint64_t    xfer_plist_id;
    double      read_time;

    Selection_info selection_info;

    struct h5dread *prev;
    struct h5dread *next;

} H5dread;

H5dread* read = NULL;

int parse_create(char* line, Dataset_info* datasetinfo)
{
    char tmp0[LINE_MAX_LEN];
    char tmp1[LINE_MAX_LEN];
    char tmp2[LINE_MAX_LEN];
    char tmp3[LINE_MAX_LEN];
    char tmp4[LINE_MAX_LEN];

    int i;
    uint64_t    dataset_id;

    sscanf(line,"%s %s %s %llu %s", tmp0, tmp1, tmp2, &dataset_id, tmp4); 
    
    // mark valid
    for(i = 0; i < cur_datasetid; i++) {
        if(datasetinfo[i].dataset_id == dataset_id) {
            datasetinfo[i].valid = 0;
            break;
        }
    }

    return 0;
}

int parse_open(char* line, File_info* fileinfo, Dataset_info* datasetinfo)
{

    // hid_t H5Dopen2( hid_t loc_id, const char *name, hid_t dapl_id )
    // e.g. 1400716016.54314 H5Dopen2 (16777216,/Step#0/Energy,0) $83886080$ 0.10009

    int i;
    char* pch;  // parsed token
    char* pend; // for strtoull
    char tmp_char[LINE_MAX_LEN];

    // locate the next available datasetinfo struct
    int id = cur_datasetid;
        
    // parse using strtok
    pch = strtok(line, " ");
    // skip walltime
    
    // op name
    pch = strtok(NULL, " ");
    strcpy(tmp_char, pch);

    
    if(strcmp(tmp_char,"H5Fopen") == 0) {
        // H5Fopen()
        pch = strtok(NULL, ",");
        pch++;      // skip"("
        strcpy(fileinfo[cur_fileid].filename, pch);

        pch = strtok(NULL, ")");

        pch = strtok(NULL, " ");
        fileinfo[cur_fileid].file_id = strtoull(pch, &pend ,10);

        // set current file as valid
        // invilidate after closing this file
        fileinfo[cur_fileid].valid = 1;

        cur_fileid++;
        
        // mark this to enable get_spase parsing
        validgetspace = 1;
        
        
        return 0;
    }
    else {
        pch = strtok(NULL, ",");
        pch++;
        datasetinfo[id].loc_id = strtoull(pch, &pend ,10);
    
        // search for corresponding file
        for(i = cur_fileid - 1; i >= 0; i--) {
            if(fileinfo[i].file_id == datasetinfo[id].loc_id && fileinfo[i].valid == 1)
                break;
        }

        strcpy(datasetinfo[id].name, fileinfo[i].filename);

        if(strcmp(tmp_char,"H5Dopen2") == 0) {
        
            pch = strtok(NULL, ",");
            if(*pch != '/')
                strcat(datasetinfo[id].name, "/");
            strcat(datasetinfo[id].name, pch);
            // continue for dapl_id
            pch = strtok(NULL, ")");
            datasetinfo[id].dapl_id = strtoull(pch, &pend ,10);
        }
        else if(strcmp(tmp_char,"H5Dopen1") == 0){
            pch = strtok(NULL, ")");
            if(*pch != '/')
                strcat(datasetinfo[id].name, "/");
            strcat(datasetinfo[id].name, pch);
        }


    }
    // set dataset valid
    datasetinfo[id].valid = 1;

    pch = strtok(NULL, " ");
    datasetinfo[id].dataset_id = strtoull(pch, &pend ,10);


    pch = strtok(NULL, "\n");
    datasetinfo[id].open_time = atof(pch);

    cur_datasetid++;

    return 0;
}


int parse_close(char* line, File_info* fileinfo, Dataset_info* datasetinfo)
{
    int i;
    char* pch;  // parsed token
    char* pend; // for strtoull
    char tmp_char[LINE_MAX_LEN];

    uint64_t tmp_fileid;
        
    // parse using strtok
    pch = strtok(line, " ");
    // skip walltime
    
    // op name
    pch = strtok(NULL, " ");
    strcpy(tmp_char, pch);

    pch = strtok(NULL, ")");
    pch++;

    tmp_fileid = strtoull(pch, &pend ,10);

    if(strcmp(tmp_char, "H5Fclose") == 0) {
        // invilidate file info
        for(i = cur_fileid - 1; i >= 0; i--){
            if(fileinfo[i].file_id == tmp_fileid && fileinfo[i].valid == 1) {
                fileinfo[i].valid = 0;
                validgetspace = 0;
                break;
            }
        }

    }
    else if(strcmp(tmp_char, "H5Dclose") == 0) {

        // invilidate dataset info
        for(i = cur_datasetid - 1; i >= 0; i--){
            if(datasetinfo[i].dataset_id == tmp_fileid && datasetinfo[i].valid == 1) {
                datasetinfo[i].valid = 0;
                break;
            }
        }

    }
    else
        return -1;


    return 0;


}

int parse_get_space(char* line, Selection_info* selectioninfo, Dataset_info* datasetinfo)
{

    // hid_t H5Dopen2( hid_t loc_id, const char *name, hid_t dapl_id )
    // e.g. 1400716016.54314 H5Dopen2 (16777216,/Step#0/Energy,0) $83886080$ 0.10009

    int i;
    char* pch;  // parsed token
    char* pend; // for strtoull
    char tmp_char[LINE_MAX_LEN];
    uint64_t dataset_id;
    int  found_match;
        

    if(validgetspace != 1)
        return 0;

    // locate the next available datasetinfo struct
    int id = cur_selectionid;

    // parse using strtok
    pch = strtok(line, " ");        // skip walltime

    pch = strtok(NULL, " ");        // skip "H5Dget_space"

    pch = strtok(NULL, ")");
    pch++;
    dataset_id = strtoull(pch, &pend ,10);

    // now search datasetinfo to match dataset_id
    found_match = 0;
    for(i = 0; i < cur_datasetid; i++) {
        if(datasetinfo[i].dataset_id == dataset_id) {
            if(datasetinfo[i].valid < 1) {
                validselect = 0;
            }
            else{
                validselect++;
                found_match = 1;
                break;
            }
        }
    }
    // result is selectioninfo[i]
    
    if(found_match == 0)
        return -1;


    strcpy(selectioninfo[id].name, datasetinfo[i].name);


    pch = strtok(NULL, " ");
    selectioninfo[id].space_id = strtoull(pch, &pend ,10);


    pch = strtok(NULL, "\n");
    // omit time


    cur_selectionid++;


    return 0;
}

int parse_hyperslab(char* line, Selection_info* selectioninfo)
{

    // record format
    // 1400615595.22573 H5Sselect_hyperslab (67110121,H5S_SELECT_SET,
    //      {1835008,...},{32768,...},{1,...},{32768, ...}) 0 0.00000


    int i, dim;
    char* pch;  // parsed token
    char* pend; // for strtoull
    char tmp_char[LINE_MAX_LEN];
    uint64_t tmp_space_id;
    double   tmp_walltime;
    int      match_id;    
    int      check_found;


    if(validselect < 1)
        return -1;
    
    // first find out the dimension by counting "," between first {} 
    dim = 1;
    for(i = 0; line[i]; i++) {
        if(line[i] == '{') {
            while(line[i] != '}')  {
                if(line[i] == ',')                
                    dim++;
                i++;
            }
            break;
        }
    }

    
    // parse using strtok
    pch = strtok(line, " ");
    tmp_walltime = atof(pch);

    pch = strtok(NULL, " ");
    strcpy(tmp_char, pch);

    pch = strtok(NULL, ",");
    pch++;
    tmp_space_id = strtoull(pch, &pend ,10);

    // now search selectioninfo to match space_id 
    check_found = 0;
    for(match_id = cur_selectionid; match_id >= 0; match_id--) {
        if(selectioninfo[match_id].space_id == tmp_space_id) {
            check_found = 1;
            break;
        }
    }

    // no need to recored a select on selectioninfo that hasn't been created (case of write)
    if(check_found == 0)
        return -1;
    
    selectioninfo[match_id].dim = dim;
    selectioninfo[match_id].walltime = tmp_walltime;
    
    
    pch = strtok(NULL, ",");
    strcpy(selectioninfo[match_id].op, pch);


    // start
    pch = strtok(NULL, "{");
    for(i = 0; i < dim; i++) {
        if(i == 0)
            selectioninfo[match_id].start[i] = strtoull(pch, &pend ,10);
        else {
            // skip ","
            pend++;
            selectioninfo[match_id].start[i] = strtoull(pend, &pend ,10);
        }
    }
    pch = strtok(NULL, "}");

    // HTEST
    if(selectioninfo[match_id].start[i-1] == 4161536)
        i = 0;

    // stride
    for(i = 0; i < dim; i++) {
        // deal with NULL
        if(strcmp(pch, "NULL") == 0) {
            selectioninfo[match_id].stride[i] = 1;
            continue;
        }
        if(i == 0)
            selectioninfo[match_id].stride[i] = strtoull(pch, &pend ,10);
        else {
            // skip ","
            pend++;
            selectioninfo[match_id].stride[i] = strtoull(pend, &pend ,10);
        }
 
    }
    pch = strtok(NULL, "}");


    //count
    pch += 2;   // skip "},"
    for(i = 0; i < dim; i++) {
        if(i == 0)
            selectioninfo[match_id].count[i] = strtoull(pch, &pend ,10);
        else {
            // skip ","
            pend++;
            selectioninfo[match_id].count[i] = strtoull(pend, &pend ,10);
        }
    }
    pch = strtok(NULL, "}");


    //block
    pch += 2;   // skip "},"
    for(i = 0; i < dim; i++) {
        // deal with NULL
        if(strcmp(pch, "NULL") == 0) {
            selectioninfo[match_id].block[i] = 1;
            continue;
        }
        if(i == 0)
            selectioninfo[match_id].block[i] = strtoull(pch, &pend ,10);
        else {
            // skip ","
            pend++;
            selectioninfo[match_id].block[i] = strtoull(pend, &pend ,10);
        }
    }

    pch =  NULL;
    pend = NULL;

    validselect--;

    return 0;
}

int print_selectioninfo(Selection_info* selectioninfo, int num)
{
    int i, j, dim;
    for(i = 0; i < num; i++) {
        
        if( selectioninfo[i].select_type  == 'E') {
            // elements

        }
        else {
            // hyperslab

            printf(" hyperslab %s, {", selectioninfo[i].name);

            dim = selectioninfo[i].dim;
            for(j = 0; j < dim; j++) {
                if(j==0)
                    printf("%llu", selectioninfo[i].start[j]);
                else
                    printf(",%llu", selectioninfo[i].start[j]);
            }
            printf("},{");
            
            for(j = 0; j < dim; j++) {
                if(j==0)
                    printf("%llu", selectioninfo[i].stride[j]);
                else
                    printf(",%llu", selectioninfo[i].stride[j]);
            }
            printf("},{");

            for(j = 0; j < dim; j++) {
                if(j==0)
                    printf("%llu", selectioninfo[i].count[j]);
                else
                    printf(",%llu", selectioninfo[i].count[j]);
            }
            printf("},{");
 
            for(j = 0; j < dim; j++) {
                if(j==0)
                    printf("%llu", selectioninfo[i].block[j]);
                else
                    printf(",%llu", selectioninfo[i].block[j]);
            }
            printf("}");
        }
 
    }

    return 0;
}

int parse_read(char* line, Selection_info* selectioninfo)
{
    // H5dread format
    // 1400542754.21204 H5dread 
    // (83886080,H5T_IEEE_F32LE,67108870,67108866,167772183,73576952) 0 0.60897
    // (hid_t dataset_id, hid_t mem_type_id, hid_t mem_space_id, hid_t file_space_id, hid_t xfer_plist_id, void * buf )

    char* pch;  // parsed token
    char* pend; // for strtoull
    char tmp_char[LINE_MAX_LEN];

    H5dread* tmp_read = (H5dread*)malloc(sizeof(H5dread));
    if(tmp_read == NULL){
        printf("Unable to allocate space for read records!\nExiting...\n");
        exit(-1);
    }


    // walltime
    pch = strtok(line, " ");
    tmp_read->walltime = atof(pch);

    // op (no use)
    pch = strtok(NULL, " ");
    strcpy(tmp_char, pch);

    // dataset_id
    pch = strtok(NULL, ",");
    pch++;      // skip "("
    tmp_read->dataset_id = strtoull(pch, &pend ,10);

    // mem_type_id
    pch = strtok(NULL, ",");
    strcpy(tmp_read->mem_type_id, pch);

    // mem_space_id
    pch = strtok(NULL, ",");
    tmp_read->mem_space_id = strtoull(pch, &pend ,10);

    // file_space_id
    pch = strtok(NULL, ",");
    tmp_read->file_space_id = strtoull(pch, &pend ,10);

    // xfer_plist_id
    pch = strtok(NULL, ",");
    tmp_read->xfer_plist_id = strtoull(pch, &pend ,10);


    // buf (no use)
    pch = strtok(NULL, " ");
    //strcpy(tmp_char, pch);

    pch = strtok(NULL, " ");
    //strcpy(tmp_char, pch);

    pch = strtok(NULL, "\n");
    
    // read_time
    tmp_read->read_time = atof(pch);


    int i;
    for(i = cur_selectionid - 1; i >=0; i--) {

        if(tmp_read->file_space_id == selectioninfo[i].space_id) {
            memcpy(&tmp_read->selection_info, &selectioninfo[i], sizeof(read->selection_info));
            break;
        }

    }

    DL_APPEND(read, tmp_read);

    return 0;
}


void print_read()
{

    int i;
    H5dread* elt;

    DL_FOREACH(read,elt){
        print_selectioninfo(&(elt->selection_info), 1);
        printf("\t %f\n", elt->read_time);
    }

}

int read_log_from_file(char *filepath, int num_file)
{
    int i;
    char filename[FILE_PATH_LEN];
    char tmp_line[LINE_MAX_LEN];

    File_info*         fileinfo;
    Dataset_info*      datasetinfo;
    Selection_info*    selectioninfo;

    
    // init fileinfo struct array
    fileinfo = malloc(sizeof(File_info) * MAX_FILE_PER_LOG);
    
    // init datasetinfo struct array
    datasetinfo = malloc(sizeof(Dataset_info) * MAX_DATASET_PER_LOG);
    
    // init selectioninfo struct array
    selectioninfo = malloc(sizeof(Selection_info) * MAX_SELECTION_PER_LOG);

    cur_fileid      = 0;
    cur_datasetid   = 0;
    cur_selectionid = 0;

    // iterate all files
    for(i = 0; i < num_file; i++) {
        validselect     = 0;
        validgetspace   = 0;

        // print progress
        sprintf(filename, "%s/log.%d", filepath, i);
        printf("Processing %s\n", filename);


        FILE *fp = fopen(filename, "r");
        if (fp == NULL) {
            printf("Error opening file %s", filename);
            return -1;
        }

    
        // start parsing log
        while(fgets(tmp_line, LINE_MAX_LEN, fp) != NULL ) {
 
            if(strstr(tmp_line, "open") != NULL) {

                parse_open(tmp_line, fileinfo, datasetinfo);

            }
            else if(strstr(tmp_line, "H5Dcreate1") != NULL || strstr(tmp_line, "H5Dcreate2") != NULL) {

                parse_create(tmp_line, datasetinfo);

            }
            else if(strstr(tmp_line, "H5Dget_space") != NULL) {

                parse_get_space(tmp_line, selectioninfo, datasetinfo);

            }
            else if(strstr(tmp_line, "H5Sselect_hyperslab") != NULL) {
                
                parse_hyperslab(tmp_line, selectioninfo); 

            }
            else if((strstr(tmp_line, "H5Dread") != NULL)) {

                parse_read(tmp_line,selectioninfo);

            }
            else if((strstr(tmp_line, "H5Fclose") != NULL)) {

                parse_close(tmp_line, fileinfo, datasetinfo);

            }

        } // within a log file
       
        fclose(fp);
    } // end reading from one log file
 

    // HTEST: print all parsed info from one file (proc)
    printf("Read accesses:\n");
    print_read(read);
    printf("\n");
    

    
    // garbage collection
    free(fileinfo);
    free(datasetinfo);
    free(selectioninfo);

    return 0;
}

void free_read()
{
    H5dread* elt;
    H5dread* tmp;

    DL_FOREACH_SAFE(read,elt,tmp) {
        DL_DELETE(read, elt);
    }
}

void print_usage()
{
    printf("Usage:\n ./merger /path/to/file #file");

}

int main(int argc, char* argv[])
{

    char *filepath;
    int num_file;
    int i, j, k;



    // argc check
    if(argc != 3) {
        print_usage();
        return -1;
    }


    // parse argv
    filepath = argv[1];
    num_file = atoi(argv[2]);


    // read log and parse
    read_log_from_file(filepath, num_file);


    free_read();

    return 0;
}
