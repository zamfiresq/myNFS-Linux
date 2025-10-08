const FILENAME_LENGTH = 128;
const DATA_LENGTH = 1024;
const DIRNAME_LENGTH = 256;
const MAX_FILES = 10;
const MAX_FILENAMES_LENGTH = 128;

struct request {
   string filename<FILENAME_LENGTH>;
   int start;
   int src_offset;
   int dest_offset;
   int size;
};

struct chunk {
   string filename<FILENAME_LENGTH>;
   opaque data<DATA_LENGTH>;
   int size;
   int dest_offset;
};
typedef struct chunk chunk;

struct opendir_args {
   string dirname<DIRNAME_LENGTH>;
};

struct readdir_args {
   string dirname<DIRNAME_LENGTH>;
};

struct readdir_result {
   string filenames<MAX_FILES>;
   bool more;
};

program NFS_PROGRAM {
   version NFS_VERSION_1 {
      string ls(string str) = 1;
      int create(string filename) = 2;
      int delete(string filename) = 3;
      chunk retrieve_file(request*) = 4;
      int send_file(chunk *) = 5;
      int mynfs_mkdir(string dirname) = 6;
      int mynfs_open(string filename) = 7;
      int mynfs_close(string filename) = 8;
      chunk mynfs_read(request*) = 9;
      int mynfs_write(chunk*) = 10;
      int mynfs_opendir(opendir_args*) = 11;
      readdir_result mynfs_readdir(readdir_args*) = 12;
   } = 1;
} = 0x21000001;

