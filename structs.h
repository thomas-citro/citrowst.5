struct time{
	int nanoseconds;
	int seconds;
};

typedef struct {
       	int allocated[20];
       	int request[20];
} resourceDescriptor;

typedef struct {
	int max[20];
	int available[20];
	int termF;
	int requestF;
	int releaseF;
	int index;
} resourceInfo;

typedef struct shmStruct {
    resourceDescriptor descriptor[18 + 1];
	resourceInfo resourceStruct;
	struct time time;
} sm;