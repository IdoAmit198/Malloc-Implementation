#include <cstddef>
#include <unistd.h>
#include <stdbool.h>
#include <cstdlib>
#include <cstring>
#include <iostream>





typedef struct MallocMetadata{
    size_t size;
    bool is_free;
    void* addr;
    struct MallocMetadata* next;
    struct MallocMetadata* prev;
}MallocMetadata;



class BlockList{
    private:
    MallocMetadata* head;
    MallocMetadata* tail;
    public:
        BlockList() : head(NULL) , tail(NULL){}
        
        void* allocateBlock(size_t size)
        {
            size_t real_size=size+sizeof(MallocMetadata);
            if(head==NULL)//this is the first block
            {
                head=(MallocMetadata*)(sbrk(sizeof(MallocMetadata)));
                void* new_address=(void*)(sbrk(size));
                if(head==(MallocMetadata*)(-1) || new_address==(void*)(-1))
                    return NULL;
                head->prev=NULL;
                head->next=NULL;
                head->size=real_size;
                head->is_free=false;
                head->addr=new_address;
                tail=head;
                return new_address;
            }
            else
            {
                MallocMetadata* ptr=head;
                while(ptr!=NULL && (ptr->size<real_size || ptr->is_free==false))
                    ptr=ptr->next;
                if(ptr!=NULL)
                {
                    ptr->is_free=false;
                    return ptr->addr;
                }
                else{
                    MallocMetadata* meta_data_ptr=(MallocMetadata*)(sbrk(sizeof(MallocMetadata)));
                    void* new_address=(void*)(sbrk(size));
                    if(meta_data_ptr==(MallocMetadata*)(-1) || new_address==(void*)(-1))
                        return NULL;
                    meta_data_ptr->prev=tail;
                    meta_data_ptr->next=NULL;
                    tail->next=meta_data_ptr;
                    meta_data_ptr->size=real_size;
                    meta_data_ptr->is_free=false;
                    meta_data_ptr->addr=new_address;
                    tail=meta_data_ptr;
                    return new_address;
                }
            }
        }

        void ListFree(void* p)
        {
            MallocMetadata* md_to_free=(MallocMetadata*)((long)p-sizeof(MallocMetadata));
            md_to_free->is_free=true;
        }
        
        void* rellocateBlock(void* oldp,size_t size)
        {
            MallocMetadata* ptr=(MallocMetadata*)((long)oldp-sizeof(MallocMetadata));
            if(size+sizeof(MallocMetadata) <= ptr->size)
                return oldp;
            void* new_ptr = allocateBlock(size);
            if(new_ptr==NULL)
                return NULL;
            else
            {
                memmove(new_ptr,oldp,ptr->size-sizeof(MallocMetadata));
                ListFree(oldp);
                return new_ptr;
            }
        }
        
        size_t numFreeBlocks()
        {
            size_t counter=0;
            MallocMetadata* ptr=head;
            while(ptr!=NULL)
            {
                if(ptr->is_free)
                    counter++;
                ptr=ptr->next;
            }
            return counter;
        }

        size_t numFreeBytes()
        {
            size_t counter=0;
            MallocMetadata* ptr=head;
            while(ptr!=NULL)
            {
                if(ptr->is_free)
                    counter+=ptr->size-sizeof(MallocMetadata);
                ptr=ptr->next;
            }
            return counter;
        }

        size_t numAllocatedBlocks()
        {
            size_t counter=0;
            MallocMetadata* ptr=head;
            while(ptr!=NULL)
            {
                counter++;
                ptr=ptr->next;
            }
            return counter;
        }

        size_t numAllocatedBytes()
        {
            size_t counter=0;
            MallocMetadata* ptr=head;
            while(ptr!=NULL)
            {
                counter+=ptr->size-sizeof(MallocMetadata);
                ptr=ptr->next;
            }
            return counter;
        }

        size_t numMetaDataBytes()
        {
            size_t counter=0;
            MallocMetadata* ptr=head;
            while(ptr!=NULL)
            {
                counter+=sizeof(MallocMetadata);
                ptr=ptr->next;
            }
            return counter;
        }

        size_t numMetaData()
        {
            return sizeof(MallocMetadata);
        }
        
};


BlockList block_list;

void* smalloc(size_t size)
{
    if(size==0 || size>100000000)
        return NULL;
    return block_list.allocateBlock(size);  
}

void* scalloc(size_t num,size_t size)
{
    void* ptr=smalloc(num*size);
    if(ptr==NULL)
        return NULL;
    return memset(ptr,0,num*size);
}

void sfree(void* p)
{
    if(p==NULL)
        return;
    block_list.ListFree(p);
}

void* srealloc(void* oldp, size_t size)
{
    if(size==0 || size>100000000)
        return NULL;
    if(oldp==NULL)
        return smalloc(size);
    return block_list.rellocateBlock(oldp,size);
}

size_t _num_free_blocks()
{
    return block_list.numFreeBlocks();
}

size_t _num_free_bytes()
{
    return block_list.numFreeBytes();
}

size_t _num_allocated_blocks()
{
    return block_list.numAllocatedBlocks();
}

size_t _num_allocated_bytes()
{
    return block_list.numAllocatedBytes();    
}

size_t _num_meta_data_bytes()
{
    return block_list.numMetaDataBytes();    
}

size_t _size_meta_data()
{
    return block_list.numMetaData();    
}


