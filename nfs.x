const MAX_FILENAME_LENGTH = 128;
const MAX_FILES           = 50;
const MAX_PATH_LENGTH     = 4096;


struct request {
    string filename<MAX_FILENAME_LENGTH>;
    unsigned int size;
    unsigned int src_offset;
    unsigned int dest_offset;
};

struct chunk {
    string filename<MAX_FILENAME_LENGTH>;
    opaque data<>;            /* payload variabil */
    int    size;
    unsigned int dest_offset;
};


typedef string filename_t<MAX_FILENAME_LENGTH>;

struct readdir_args {
    string dirname<MAX_PATH_LENGTH>;
};

struct readdir_result {
    filename_t filenames<MAX_FILES>;
};


program NFS_PROGRAM {
    version NFS_VERSION_1 {
        string          ls(string)                    = 1;
        int             create(string)                = 2;
        int             delete(string)                = 3;
        chunk           retrieve_file(request)        = 4;
        int             send_file(chunk)              = 5;

        int             mynfs_mkdir(string)           = 6;
        int             mynfs_remdir(string)          = 7;

        /* „aliasuri” prietenoase către retrieve/send */
        chunk           mynfs_read(request)           = 8;
        int             mynfs_write(chunk)            = 9;

        readdir_result  mynfs_readdir(readdir_args)   = 10;
    } = 1;
} = 0x21000001;