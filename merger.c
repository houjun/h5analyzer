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
#define MAX_NUM_LOG             2048
#define MAX_SELECTION_PER_LOG   2048

int cur_fileid;
int cur_datasetid;                      // current datasetID opened by H5Dopen* for current log
int cur_selectionid;                    // current selectionID for current log

int validselect;                        // H5Sselect_* is parsed only when previous H5Dget_space is 
                                        // valid (when it is reading)
int validgetspace;                                        


typedef struct file_info {

    int         valid;
    uint64_t    file_id;
    char        filename[FILE_PATH_LEN];
    char        username[FILE_PATH_LEN];

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
    char        username[FILE_PATH_LEN];
    uint64_t    dapl_id;                // only used for H5Dopen2
    double      open_time;

} Dataset_info;

// each H5Dget_space* will create a Selection_info struct
typedef struct selection_info {
    
    //Dataset_info*  datasetinfo_ptr;

    char        name[FILE_PATH_LEN];    // /path/to/file/dataset_name
    char        username[FILE_PATH_LEN];

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

typedef struct Pid_list {
    int pid;
    struct Pid_list* prev;
    struct Pid_list* next;
} Pid_list;

typedef struct h5dread {

    double      walltime;
    uint64_t    dataset_id;
    char        mem_type_id[OP_NAME_LEN];
    uint64_t    mem_space_id;
    uint64_t    file_space_id;
    uint64_t    xfer_plist_id;
    double      read_time;

    Selection_info selection_info;

    int         pid;

    // used for pattern
    Pid_list*   pids;
    int         merged;
    int         repeat_time;

    struct h5dread *prev;
    struct h5dread *next;

} H5dread;

H5dread* read[MAX_DATASET_PER_LOG];

H5dread* local_pattern[MAX_DATASET_PER_LOG];
H5dread* global_pattern[MAX_DATASET_PER_LOG];

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

        pch = strtok(NULL, ",");
        pch = strtok(NULL, ")");

        pch = strtok(NULL, " ");
        fileinfo[cur_fileid].file_id = strtoull(pch, &pend ,10);

        // skip time
        pch = strtok(NULL, " ");
        // user name
        pch = strtok(NULL, "\n");
        strcpy(fileinfo[cur_fileid].username, pch);


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
        strcpy(datasetinfo[id].username, fileinfo[i].username);

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

    // dataset ID
    pch = strtok(NULL, " ");
    datasetinfo[id].dataset_id = strtoull(pch, &pend ,10);

    // open time
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
    //hid_t H5Dget_space( hid_t dataset_id )

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
    for(i = cur_datasetid - 1; i >= 0 ; i--) {
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

    strcpy(selectioninfo[id].username, datasetinfo[i].username);


    pch = strtok(NULL, " ");
    selectioninfo[id].space_id = strtoull(pch, &pend ,10);


    pch = strtok(NULL, "\n");
    // omit time

    pch = strtok(NULL, "\n");

    cur_selectionid++;


    return 0;
}

int parse_hyperslab(char* line, Selection_info* selectioninfo)
{
    // record format
    // 1400615595.22573 H5Sselect_hyperslab (67110121,H5S_SELECT_SET,
    //      {1835008,...},{32768,...},{1,...},{32768, ...}) 0 0.00000


    int i, j, dim;
    char* pch;  // parsed token
    char* pend; // for strtoull
    char tmp_char[LINE_MAX_LEN];
    uint64_t tmp_space_id;
    double   tmp_walltime;
    int      match_id;    
    int      check_found;


    if(validselect < 1)
        return -1;
    
    // first find out the dimension by counting ";" between first {} 
    dim = 1;
    for(i = 0; line[i]; i++) {
        if(line[i] == '{') {
            while(line[i] != '}')  {
                if(line[i] == ';')                
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

    // stride
    for(i = 0; i < dim; i++) {
        // deal with NULL
        if(strcmp(pch, "NULL") == 0) {
            for(j = i; j < dim; j++)
                selectioninfo[match_id].stride[j] = 1;
            break;
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
            for(j = i; j < dim; j++)
                selectioninfo[match_id].block[j] = 1;
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

    selectioninfo[match_id].select_type = 'H';

    pch =  NULL;
    pend = NULL;

    validselect--;

    return 0;
}

int get_stat(double* stat, int dataset_id)
{
    H5dread* elt;
    int i, dim, count;
    double min, max, mean, blk_size;

    min   = 999999999999.0;
    max   = 0.0;
    mean  = 0.0;
    count = 0;

    DL_FOREACH(read[dataset_id],elt) {

        dim = elt->selection_info.dim;
        blk_size = 1.0;
        for(i = 0; i< dim; i++) {
            blk_size *= (elt->selection_info.block[i]*elt->selection_info.count[i]);
        }

        if(blk_size > max)
            max = blk_size;

        if(blk_size < min)
            min = blk_size;
        
        mean += blk_size;

        count++;
    }

    mean /= count; 

    stat[0] = min;
    stat[1] = mean;
    stat[2] = max;

    return 0;
}


void print_read()
{
    int i;
    H5dread* elt;

    for(i = 0; i < MAX_DATASET_PER_LOG; i++) {
        if(read[i] == NULL)
            break;

        printf("\n%s\n", read[i]->selection_info.name);
        DL_FOREACH(read[i],elt){
            if(elt == NULL)
                break;
            print_selectioninfo(&(elt->selection_info), 1);
            printf("\t\t%f\n", elt->read_time);
        }
    }
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

            printf("hyperslab {");

            dim = selectioninfo[i].dim;
            for(j = 0; j < dim; j++) {
                if(j==0)
                    printf("%8llu", selectioninfo[i].start[j]);
                else
                    printf(",%8llu", selectioninfo[i].start[j]);
            }
            printf("} , {");
            
            for(j = 0; j < dim; j++) {
                if(j==0)
                    printf("%8llu", selectioninfo[i].stride[j]);
                else
                    printf(",%8llu", selectioninfo[i].stride[j]);
            }
            printf("} , {");

            for(j = 0; j < dim; j++) {
                if(j==0)
                    printf("%8llu", selectioninfo[i].count[j]);
                else
                    printf(",%8llu", selectioninfo[i].count[j]);
            }
            printf("} , {");
 
            for(j = 0; j < dim; j++) {
                if(j==0)
                    printf("%8llu", selectioninfo[i].block[j]);
                else
                    printf(",%8llu", selectioninfo[i].block[j]);
            }
            printf("}");
        }
 
    }

    return 0;
}


void print_pattern()
{
    int i;
    double stat[3];
    H5dread* elt;
    Pid_list* pidlist;

    printf("\nFile/dataset name\n[Proc]\tSelector\t\t\tRegion\t\t\t\tTotal time  Repeat time  [Min, Mean, Max elem]\n");

    printf("\nLocal Pattern");
    for(i = 0; i < MAX_DATASET_PER_LOG; i++) {
        if(local_pattern[i] == NULL)
            break;

        printf("\n%s\n", local_pattern[i]->selection_info.name);
        DL_FOREACH(local_pattern[i],elt){
            if(elt == NULL)
                break;
            printf("[ %s - %d ]: ",elt->selection_info.username, elt->pid);
            print_selectioninfo(&(elt->selection_info), 1);
            printf("\t %f \t %d", elt->read_time, elt->repeat_time);

            // get max, min, mean
            get_stat(stat, i);
            printf("\t[ %.0f %.0f %.0f ]\n",stat[0], stat[1], stat[2]);
        }
    }


    printf("\nGlobal Pattern");
    for(i = 0; i < MAX_DATASET_PER_LOG; i++) {
        if(global_pattern[i] == NULL)
            break;

        printf("\n%s\n", global_pattern[i]->selection_info.name);
        DL_FOREACH(global_pattern[i],elt){
            if(elt == NULL)
                break;
            DL_SORT(elt->pids, cmp_pid);
            printf("[ %s -",elt->selection_info.username);
            DL_FOREACH(elt->pids, pidlist)
                printf(" %d",pidlist->pid);
            printf(" ]:\t");

            print_selectioninfo(&(elt->selection_info), 1);
            
            printf("\t %f \t %d", elt->read_time, elt->repeat_time);

            // print pids
            // sort first
            // get max, min, mean
            get_stat(stat, i);
            printf("\t[ %.0f %.0f %.0f ]\n",stat[0], stat[1], stat[2]);
        }
    }
}

int parse_read(char* line, Selection_info* selectioninfo, int pid)
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
    memset(tmp_read, 0, sizeof(H5dread));


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

    tmp_read->pid = pid;

    int i;
    for(i = cur_selectionid - 1; i >=0; i--) {

        if(tmp_read->file_space_id == selectioninfo[i].space_id) {
            memcpy(&tmp_read->selection_info, &selectioninfo[i], sizeof(tmp_read->selection_info));
            break;
        }

    }

    // find the right place to append
    for(i = 0; i < MAX_DATASET_PER_LOG; i++) {
        if(read[i] != NULL) {
            if( strcmp(read[i]->selection_info.name, tmp_read->selection_info.name) == 0 ) {
                DL_APPEND(read[i], tmp_read);
                break;
            }
        }
        else {

            DL_APPEND(read[i], tmp_read);
            break;
        }
    }

    return 0;
}

int cmp_read(H5dread* a, H5dread* b)
{
    if(a->selection_info.start[0] == b->selection_info.start[0])
        return 0;
    else
        return a->selection_info.start[0] < b->selection_info.start[0] ? -1 : 1;
}

int cmp_pid(Pid_list* a, Pid_list* b)
{
    if(a->pid == b->pid)
        return 0;
    else if(a->pid > b->pid)
        return 1;
    else
        return -1;

    return 0;
}

int cmp_pattern(H5dread* x, H5dread* y)
{
    int dim, i;

    Selection_info* a = &x->selection_info;
    Selection_info* b = &y->selection_info;

    if(a->dim != b->dim)
        return 0;

    dim = a->dim;

    if(a->select_type != b->select_type) 
        return 0;

    if(a->select_type == 'H') {
        for(i = 0; i < dim; i++) {
            if( (a->start[i] != b->start[i]) || (a->stride[i] != b->stride[i]) ||
                    (a->count[i] != b->count[i]) || (a->block[i] != b->block[i])) {

                return 0;
            }

        }

    }


    return 1;
}

int merge_read(char pattern_type)
{
    int i, j, k, dim, checkmark, flag;
    H5dread* elt;
    H5dread* elt_n;
    H5dread* elt_r;

    H5dread* tmp;

    Pid_list* tmp_pid;
    Pid_list* search_pid;

    for(i = 0; i < MAX_DATASET_PER_LOG; i++) {
    // within one dataset
    
        if(pattern_type == 'G')
            elt = local_pattern[i];
        else
            elt = read[i];

        while(elt != NULL) {
            if(elt->merged == 1) {
                elt = elt->next;
                continue;
            }

            tmp = (H5dread*)malloc(sizeof(H5dread));
            memcpy(tmp, elt, sizeof(H5dread));

            tmp->prev = NULL;
            tmp->next = NULL;
            tmp->merged = 0;
            tmp->pids = NULL;

            if(pattern_type == 'L')
                tmp->repeat_time = 1;

            tmp_pid = (Pid_list*)malloc(sizeof(Pid_list));
            tmp_pid->pid = tmp->pid;
            DL_APPEND(tmp->pids, tmp_pid);

            elt_n = elt->next;

            dim = tmp->selection_info.dim;

            // merge as many subsequent accesses as possible
            checkmark = 0;

            while(elt_n != NULL){

            
                if(pattern_type == 'L' && elt_n->pid != elt->pid) {
                    elt_n = elt_n->next;
                    continue;
                }

                if(checkmark >= dim )
                    break;
               
                if( elt_n->merged == 1) {
                    elt_n = elt_n->next;
                    continue;
                }

                // check for each dimension
                for(j = 0; j < dim; j++) {
                    
                    if(elt_n->selection_info.start[j] > (tmp->selection_info.start[j] + tmp->selection_info.block[j]*tmp->selection_info.count[j]) )
                        checkmark++;
                    else if(elt_n->selection_info.start[j] == (tmp->selection_info.start[j] + tmp->selection_info.block[j]*tmp->selection_info.count[j])) {

                        // merge when only one dimension is extendable while the other are the same
                        flag = 0;
                        for(k = 0; k < dim; k++) {
                            if(k == j)
                                continue;

                            if(elt_n->selection_info.start[j] != tmp->selection_info.start[j] || elt_n->selection_info.stride[j] != tmp->selection_info.stride[j]
                                    || elt_n->selection_info.block[j]*tmp->selection_info.count[j] != tmp->selection_info.block[j]*tmp->selection_info.count[j]) {
                                flag = 1;
                                break;
                            }
                        }

                        if(flag == 0) {
                            // can be merged
                            if(tmp->selection_info.stride[j] > tmp->selection_info.block[j] + elt->selection_info.block[j] )
                                tmp->selection_info.block[j] += elt_n->selection_info.block[j];
                            else
                                tmp->selection_info.count[j] += elt_n->selection_info.count[j];
    
                            tmp->read_time += elt_n->read_time;

                            tmp_pid = (Pid_list*)malloc(sizeof(Pid_list));
                            tmp_pid->pid = elt_n->pid;

                            DL_SEARCH(tmp->pids, search_pid, tmp_pid, cmp_pid);
                            if(search_pid == NULL)
                                DL_APPEND(tmp->pids, tmp_pid);
                            else
                                free(tmp_pid);

                            elt_n->merged = 1;
                        }

                        break;
                    } // else if

                } // for each dimension

                elt_n = elt_n->next;

            } // while elt_n

        
            // check for repeat
            
            if(pattern_type == 'G')
                elt_r = global_pattern[i];
            else
                elt_r = local_pattern[i];
            
            while(elt_r != NULL) {
          
                if(cmp_pattern(elt_r, tmp) == 1) {

                    if(pattern_type == 'L' && elt_r->pid != tmp->pid) {
                        elt_r = elt_r->next;
                        continue;
                    }
                    else if(pattern_type == 'G' && elt_r->pid != tmp->pid) {
                        tmp_pid = (Pid_list*)malloc(sizeof(Pid_list));
                        tmp_pid->pid = tmp->pid;
                        DL_APPEND(elt_r->pids, tmp_pid);
                    }

                    elt_r->repeat_time++;
                    elt_r->read_time += tmp->read_time;
                    free(tmp);
                    break;
                }
                elt_r = elt_r->next;
            }

            if(elt_r == NULL) {
             
                if(pattern_type == 'G')
                    DL_APPEND(global_pattern[i], tmp);
                else    
                    DL_APPEND(local_pattern[i], tmp);

            }
            elt = elt->next;

        } // while elt = read[i]

        

    } // for all datasets

    return 0;
}

void init_read()
{
    int i;
    for(i = 0; i < MAX_DATASET_PER_LOG; i++)
        read[i] = NULL;
}

void free_read()
{
    int i;
    H5dread* elt;
    H5dread* tmp;

    for(i = 0; i < MAX_DATASET_PER_LOG; i++) {
        if(read[i] == NULL)
            break;
        DL_FOREACH_SAFE(read[i],elt,tmp) {
            DL_DELETE(read[i], elt);
            free(elt);
        }
    }
}


void init_pattern()
{
    int i;
    for(i = 0; i < MAX_DATASET_PER_LOG; i++)
        global_pattern[i] = NULL;
}

void clear_merged_flag()
{
    int i;
    H5dread* iter;
    for (i = 0; i < MAX_DATASET_PER_LOG; i++) {
        iter = read[i];
        while(iter != NULL) {
            iter->merged = 0;
            iter = iter->next;

        }
    }

}
void free_pattern()
{
    int i;
    H5dread* elt;
    H5dread* tmp;
    Pid_list* pid_elt;
    Pid_list* pid_tmp;

    for(i = 0; i < MAX_DATASET_PER_LOG; i++) {
        if(local_pattern[i] == NULL)
            break;
        DL_FOREACH_SAFE(local_pattern[i],elt,tmp) {
        
            DL_FOREACH_SAFE(elt->pids, pid_elt,pid_tmp) {
                DL_DELETE(elt->pids, pid_elt);
                free(pid_elt);
            }
            DL_DELETE(local_pattern[i], elt);
            free(elt);
        }
    }

    for(i = 0; i < MAX_DATASET_PER_LOG; i++) {
        if(global_pattern[i] == NULL)
            break;
        DL_FOREACH_SAFE(global_pattern[i],elt,tmp) {
        
            DL_FOREACH_SAFE(elt->pids, pid_elt,pid_tmp) {
                DL_DELETE(elt->pids, pid_elt);
                free(pid_elt);
            }
            DL_DELETE(global_pattern[i], elt);
            free(elt);
        }
    }
}


int read_log_from_file(int num_file, char* argv[])
{
    int i, j, log_per_dir;
    char filename[FILE_PATH_LEN];
    char tmp_line[LINE_MAX_LEN];

    File_info*         fileinfo;
    Dataset_info*      datasetinfo;
    Selection_info*    selectioninfo;

    init_read();
    init_pattern();
    
    // init fileinfo struct array
    fileinfo = malloc(sizeof(File_info) * MAX_FILE_PER_LOG);
    memset(fileinfo, 0, sizeof(File_info) * MAX_FILE_PER_LOG);
    
    // init datasetinfo struct array
    datasetinfo = malloc(sizeof(Dataset_info) * MAX_DATASET_PER_LOG);
    memset(datasetinfo, 0, sizeof(Dataset_info) * MAX_DATASET_PER_LOG);
    
    // init selectioninfo struct array
    selectioninfo = malloc(sizeof(Selection_info) * MAX_SELECTION_PER_LOG);
    memset(selectioninfo, 0, sizeof(Selection_info) * MAX_SELECTION_PER_LOG);

    cur_fileid      = 0;
    cur_datasetid   = 0;
    cur_selectionid = 0;

    for (j = 0; j < num_file; j++) {
        
        log_per_dir = atoi(argv[3 + j*2]);
        // iterate all files
        for(i = 0; i < log_per_dir; i++) {
            validselect     = 0;
            validgetspace   = 0;

            // print progress
            sprintf(filename, "%s/log.%d", argv[2 + j*2], i);

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

                    parse_read(tmp_line,selectioninfo, i);

                }
                else if((strstr(tmp_line, "H5Fclose") != NULL)) {

                    parse_close(tmp_line, fileinfo, datasetinfo);

                }

            } // within a log file
           
            fclose(fp);
        } // end reading from one log file
 

    }
    merge_read('L');
    clear_merged_flag();
 
    for(i = 0; i < MAX_DATASET_PER_LOG; i++) {
        DL_SORT(read[i], cmp_read);
    }
    for(i = 0; i < MAX_DATASET_PER_LOG; i++) {
        DL_SORT(local_pattern[i], cmp_read);
    }


    merge_read('G');
    print_pattern();
    //print_read(read);
    printf("\n");
    

    
    // garbage collection
    free(fileinfo);
    free(datasetinfo);
    free(selectioninfo);

    free_read();
    free_pattern();
    return 0;
}
void print_usage()
{
    printf("Usage:\n ./merger #path /path/to/file1 #file1 /path/to/file2 #file2 ...\n");
}

int main(int argc, char* argv[])
{
    char *filepath;
    int num_file;
    int i, j, k;



    // argc check
    if(argc % 2 != 0) {
        print_usage();
        return -1;
    }


    // parse argv
    num_file = atoi(argv[1]);


    // read log and parse
    read_log_from_file(num_file, argv);



    return 0;
}
