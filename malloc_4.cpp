#include <cstddef>
#include <unistd.h>
#include <stdbool.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/mman.h>


#define sBlockManager BlockManager::instance()

#define NUM_OF_BINS 128
#define MIN_SPLIT_SIZE 128
#define BIN_SIZE 1024
#define MAX_MALLOC_SIZE 100000000
#define MMAP_ALLOCATION_MIN_SIZE 128*BIN_SIZE

size_t AlignSizeToEight(size_t size)
{
    return (size%8==0)? size : size+(8-size%8);
}

typedef struct MallocMetadata{
    size_t size;
    bool is_free;
    void* addr;
    struct MallocMetadata* list_next;
    struct MallocMetadata* list_prev;
    struct MallocMetadata* histo_next;
    struct MallocMetadata* histo_prev;
}MallocMetadata;



class SbrkBlockList{
    private:
    MallocMetadata* head;
    MallocMetadata* tail;
    
    public:
        SbrkBlockList(): head(NULL) , tail(NULL){}

        MallocMetadata* findBySizeHist(size_t size)
        {
            MallocMetadata* ptr=head;
            while(ptr!=NULL && ptr->size-AlignSizeToEight(sizeof(MallocMetadata)) <size)
                ptr=ptr->histo_next;
            return ptr;
        }

        void insertAfterBlockList(MallocMetadata* ptr_to_insert,MallocMetadata* ptr_before)
        {
            ptr_to_insert->list_next=ptr_before->list_next; // ptr->next = NULL
            ptr_to_insert->list_prev=ptr_before; // ptr->prev = tail
            if (ptr_before!= tail)
                ptr_before->list_next->list_prev=ptr_to_insert; // tail->next->prev = 
            ptr_before->list_next=ptr_to_insert; //tail->next = ptr
        }

        void insertAtListEnd(MallocMetadata* ptr)
        {
            if(head==NULL)
            {
                head=ptr;
                tail=ptr;
                head->list_next=NULL;
                head->list_prev=NULL;
                return;
            }
            insertAfterBlockList(ptr,tail);
            tail=tail->list_next;
        }
        
        void insertAfterBlockHisto(MallocMetadata* ptr_to_insert,MallocMetadata* ptr_before)
        {
            ptr_to_insert->histo_next=ptr_before->histo_next;
            ptr_to_insert->histo_prev=ptr_before;
            if (ptr_before!= tail)
                ptr_before->histo_next->histo_prev=ptr_to_insert;
            ptr_before->histo_next=ptr_to_insert;
        }

        void insertAtHistEnd(MallocMetadata* ptr)
        {
            if(head==NULL)
            {
                head=ptr;
                tail=ptr;
                head->histo_next=NULL;
                head->histo_prev=NULL;
                return;
            }
            insertAfterBlockHisto(ptr,tail);
            tail=tail->histo_next;
        }
        
        void insertAtHistBegin(MallocMetadata* ptr)
        {
            ptr->histo_next=head;
            ptr->histo_prev=NULL;
            head->histo_prev=ptr;
            head=ptr;
        }
        
        MallocMetadata* removeHisto(MallocMetadata* ptr)
        {
            //list consist of one element
            if (ptr==head && ptr==tail)
            {
                head=NULL;
                tail=NULL;
            }
            else if(ptr==head)
            {
                head=ptr->histo_next;
                ptr->histo_next->histo_prev=NULL;
            }
            else if(ptr==tail)
            {
                tail=ptr->histo_prev;
                ptr->histo_prev->histo_next=NULL;
            }
            else{
                ptr->histo_next->histo_prev=ptr->histo_prev;
                ptr->histo_prev->histo_next=ptr->histo_next;
            }
            ptr->histo_next=NULL;
            ptr->histo_prev=NULL;
            return ptr;
        }

        MallocMetadata* removeList(MallocMetadata* ptr)
        {
            if (ptr==head && ptr==tail)
            {
                head=NULL;
                tail=NULL;
            }
            else if(ptr==head)
            {
                head=ptr->list_next;
                ptr->list_next->list_prev=NULL;
            }
            else if(ptr==tail)
            {
                tail=ptr->list_prev;
                ptr->list_prev->list_next=NULL;
            }
            else{
                ptr->list_next->list_prev=ptr->list_prev;
                ptr->list_prev->list_next=ptr->list_next;
            }
            ptr->list_next=NULL;
            ptr->list_prev=NULL;
            return ptr;
        }

        void insertBySizeHisto(MallocMetadata* ptr_to_insert)
        {
            if(head==NULL) //list is empty
            {
                head=ptr_to_insert;
                tail = head;
                head->histo_next=NULL;
                head->histo_prev=NULL;
                return;
            }
            MallocMetadata* ptr=head;
            while(ptr!=NULL && ptr->size < ptr_to_insert->size)                    
                ptr=ptr->histo_next;
            if(ptr==NULL)//should be tail
                insertAtHistEnd(ptr_to_insert);
            else if(ptr==head)//should be head
                insertAtHistBegin(ptr_to_insert);
            else
                insertAfterBlockHisto(ptr_to_insert,ptr->histo_prev);
        }

        bool IsBlockFree(MallocMetadata* ptr)
        {
            return ptr->is_free;
        }

        bool IsPrevFree(MallocMetadata* ptr)
        {
            return (ptr->list_prev!=NULL && IsBlockFree(ptr->list_prev));
        }

        bool IsNextFree(MallocMetadata* ptr)
        {
            return (ptr->list_next!=NULL && IsBlockFree(ptr->list_next));
        }
        
        MallocMetadata* Wilderness()
        {
            if(tail!=NULL && tail->is_free)
                return tail;
            return NULL;
        }

        bool isLast(MallocMetadata* ptr)
        {
            return (head!=NULL && ptr==tail);
        }
        
        int numFreeBlocksList()
        {
            size_t counter=0;
            MallocMetadata* ptr=head;
            while(ptr!=NULL)
            {
                if(ptr->is_free)
                    counter++;
                ptr=ptr->histo_next;
            }
            return counter;
        }
        
        size_t numFreeBytesList()
        {
            size_t counter=0;
            MallocMetadata* ptr=head;
            while(ptr!=NULL)
            {
                if(ptr->is_free)
                    counter+=ptr->size-AlignSizeToEight(sizeof(MallocMetadata));
                ptr=ptr->histo_next;
            }
            return counter;
        }


        size_t numAllocatedBlocksList()
        {
            size_t counter=0;
            MallocMetadata* ptr=head;
            while(ptr!=NULL)
            {
                counter++;
                ptr=ptr->list_next;
            }
            return counter;
        }

        size_t numAllocatedBytesList()
        {
            size_t counter=0;
            MallocMetadata* ptr=head;
            while(ptr!=NULL)
            {
                counter+=ptr->size-AlignSizeToEight(sizeof(MallocMetadata));
                ptr=ptr->list_next;
            }
            return counter;
        }

        size_t numMetaDataBytesList()
        {
            size_t counter=0;
            MallocMetadata* ptr=head;
            while(ptr!=NULL)
            {
                counter+=AlignSizeToEight(sizeof(MallocMetadata));
                ptr=ptr->list_next;
            }
            return counter;
        }
};


class BlockManager
{
    private:
    SbrkBlockList histogram[NUM_OF_BINS];
    SbrkBlockList all_blocks_list;
    size_t mmap_allocated_blocks;
    size_t mmap_allocated_bytes;

    BlockManager() :all_blocks_list(), mmap_allocated_blocks(0), mmap_allocated_bytes(0)
    {
        for(int i=0;i<NUM_OF_BINS;i++)
            histogram[i]=SbrkBlockList();
    }
    public:

    
    static BlockManager* instance()
		{
			static BlockManager instance;
			return &instance;
		}

    int IndexOfHisto(size_t size)//size without metaData
    {
        if (size == MMAP_ALLOCATION_MIN_SIZE)
           return NUM_OF_BINS-1;
        else
            return size/BIN_SIZE;
    }
    void Split(MallocMetadata* ptr,size_t size)
    {
        MallocMetadata* md_new_free = (MallocMetadata*)((long)(ptr)+(long)(size));
        md_new_free->addr=(void*)((long)(md_new_free)+(long)(AlignSizeToEight(sizeof(MallocMetadata))));
        md_new_free->size=ptr->size-size;
        ptr->size=size;
        if(ptr->list_next==NULL)
            all_blocks_list.insertAtListEnd(md_new_free);
        else
            all_blocks_list.insertAfterBlockList(md_new_free,ptr);
        md_new_free->is_free=true;
        histogram[IndexOfHisto(md_new_free->size-AlignSizeToEight(sizeof(MallocMetadata)))].insertBySizeHisto(md_new_free);    
    }

    void* BlockAllocate(size_t size)
    {
        if(size > MMAP_ALLOCATION_MIN_SIZE)//should use mmap and not sbrk
        {
            MallocMetadata* meta_data_ptr=(MallocMetadata*)(mmap(NULL, size+AlignSizeToEight(sizeof(MallocMetadata)), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
            if(meta_data_ptr==(MallocMetadata*)(-1))
                    return NULL;
            else{
                    meta_data_ptr->addr=(void*)((long)meta_data_ptr+(long)AlignSizeToEight(sizeof(MallocMetadata)));
                    meta_data_ptr->is_free=false;
                    meta_data_ptr->size=size+AlignSizeToEight(sizeof(MallocMetadata));
                    meta_data_ptr->list_next = NULL;
                    meta_data_ptr->list_prev = NULL;
                    meta_data_ptr->histo_next = NULL;
                    meta_data_ptr->histo_prev = NULL;
                    mmap_allocated_blocks++;
                    mmap_allocated_bytes += size;
                    return meta_data_ptr->addr;
            }
        }
        size_t real_size=size+AlignSizeToEight(sizeof(MallocMetadata));
        int i= ((size==MMAP_ALLOCATION_MIN_SIZE)? NUM_OF_BINS-1 : size/BIN_SIZE);
        bool found=false;
        MallocMetadata* ptr_to_allocate_at;
        while(i<NUM_OF_BINS && !found)
        {
            ptr_to_allocate_at=histogram[i].findBySizeHist(size);
            if(ptr_to_allocate_at!=NULL)
                found=true;
            i++;
        }
        i--;
        if(!found)
        {
            MallocMetadata* free_tail = all_blocks_list.Wilderness();
            if(free_tail)
            /**
            compute how much to sbrk. - done
            sbrk right part. - done
            hist[i]removefromHisto(befor_tail) - done
            before_tail_size =real_size - done
            is_free=false
            **/
            {
                if(sbrk(real_size - free_tail->size)==(void*)-1)
                    return NULL;
                histogram[IndexOfHisto(free_tail->size-AlignSizeToEight(sizeof(MallocMetadata)))].removeHisto(free_tail);
                free_tail->size=real_size;
                free_tail->is_free=false;
                return free_tail->addr;
            }
            else
            {
                MallocMetadata* meta_data_ptr=(MallocMetadata*)(sbrk(AlignSizeToEight(sizeof(MallocMetadata))));
                void* new_address=(void*)(sbrk(size));
                if(meta_data_ptr==(MallocMetadata*)(-1) || new_address==(void*)(-1))
                    return NULL;
                meta_data_ptr->histo_next=NULL;
                meta_data_ptr->histo_prev=NULL;
                meta_data_ptr->size=real_size;
                meta_data_ptr->is_free=false;
                meta_data_ptr->addr=new_address;
                all_blocks_list.insertAtListEnd(meta_data_ptr);
                return new_address;
            }
        }
        else
        {
            //remove from histo list and update block isnt free
            histogram[i].removeHisto(ptr_to_allocate_at);
            ptr_to_allocate_at->is_free = false;
            if((long)(ptr_to_allocate_at->size-real_size)>=MIN_SPLIT_SIZE)//splitting
                Split(ptr_to_allocate_at, real_size);
            return ptr_to_allocate_at->addr;
        }
    }

    void FreeBlock(void* addrs)
        /**
        compute metaData = addrs-sizeof(metaData) in all_bblock_manager. - done
        check if addrs is released(is_free), if so then return - done
        enter freed block to histogram
        check before block if free- if so, (merge and update ptr to before one). - done
        merge: 
                check after block if free - if so,merge.
        is_free=true
        **/
    {
        MallocMetadata* md_to_free=(MallocMetadata*)((long)addrs-AlignSizeToEight(sizeof(MallocMetadata)));
        if(md_to_free->size-AlignSizeToEight(sizeof(MallocMetadata))>MMAP_ALLOCATION_MIN_SIZE) //this block is of mmap, should use unmap
        {
            mmap_allocated_blocks--;
            size_t data_size=(size_t)((size_t)md_to_free->size - (size_t)AlignSizeToEight(sizeof(MallocMetadata)));
            mmap_allocated_bytes -= data_size;
            munmap(md_to_free, md_to_free->size);
            return;
        }
        else{ //should regular free
            if (all_blocks_list.IsBlockFree(md_to_free))
                return;
            md_to_free->is_free=true;
            histogram[IndexOfHisto(md_to_free->size-AlignSizeToEight(sizeof(MallocMetadata)))].insertBySizeHisto(md_to_free);
            if(all_blocks_list.IsPrevFree(md_to_free))
                md_to_free=Merge(md_to_free->list_prev,true,false);
            if(all_blocks_list.IsNextFree(md_to_free))
                md_to_free=Merge(md_to_free,true,false);
        }
    }

    
    MallocMetadata* Merge(MallocMetadata* first_block,bool free, bool mirror)    
    {
        MallocMetadata* second_block=all_blocks_list.removeList(first_block->list_next);
        MallocMetadata* md_to_free= mirror? second_block: first_block;
        histogram[IndexOfHisto(md_to_free->size-AlignSizeToEight(sizeof(MallocMetadata)))].removeHisto(md_to_free);
        first_block->size+=second_block->size;
        if(free)
        {
            histogram[IndexOfHisto(second_block->size-AlignSizeToEight(sizeof(MallocMetadata)))].removeHisto(second_block);
            histogram[IndexOfHisto(first_block->size-AlignSizeToEight(sizeof(MallocMetadata)))].insertBySizeHisto(first_block);
        }
        else
            first_block->is_free=false;
        return first_block;
    }



    void* Rellocate(void* oldp,size_t size)
    {
        MallocMetadata* md_to_realloc=(MallocMetadata*)((long)oldp-AlignSizeToEight(sizeof(MallocMetadata)));
        if(md_to_realloc->size-AlignSizeToEight(sizeof(MallocMetadata)) > MMAP_ALLOCATION_MIN_SIZE) //should use mmap
        {
            if(size == (md_to_realloc->size -AlignSizeToEight(sizeof(MallocMetadata))))
                return oldp;
            MallocMetadata* meta_data_ptr=(MallocMetadata*)(mmap(0, size+AlignSizeToEight(sizeof(MallocMetadata)), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
            if(meta_data_ptr==(MallocMetadata*)(-1))
                    return NULL;
            mmap_allocated_bytes -= (md_to_realloc->size-AlignSizeToEight(sizeof(MallocMetadata)));
            mmap_allocated_bytes += size;
            meta_data_ptr->addr=(void*)((long)meta_data_ptr+(long)(AlignSizeToEight(sizeof(MallocMetadata))));
            size_t amount_to_cpy= (size<(md_to_realloc->size-AlignSizeToEight(sizeof(MallocMetadata))) ? size : (md_to_realloc->size-AlignSizeToEight(sizeof(MallocMetadata))));
            memmove(meta_data_ptr->addr,oldp,amount_to_cpy);
            munmap(md_to_realloc,md_to_realloc->size);
            meta_data_ptr->size=size+AlignSizeToEight(sizeof(MallocMetadata));
            return meta_data_ptr->addr;
        }
        size_t real_size=size+AlignSizeToEight(sizeof(MallocMetadata));
        size_t old_size = md_to_realloc->size-AlignSizeToEight(sizeof(MallocMetadata));
        if(md_to_realloc->size-AlignSizeToEight(sizeof(MallocMetadata)) >= size)
        {
            if(md_to_realloc->size-real_size >= MIN_SPLIT_SIZE+AlignSizeToEight(sizeof(MallocMetadata)))
                Split(md_to_realloc, real_size);
            return oldp;
        }
        else if(all_blocks_list.IsPrevFree(md_to_realloc) && md_to_realloc->size+md_to_realloc->list_prev->size-AlignSizeToEight(sizeof(MallocMetadata)) > size)
        {
            md_to_realloc=Merge(md_to_realloc->list_prev,false,false);
            if((long)(md_to_realloc->size-real_size-AlignSizeToEight(sizeof(MallocMetadata)))>=MIN_SPLIT_SIZE)
                Split(md_to_realloc, real_size);
            memmove(md_to_realloc->addr,oldp,old_size);
            return md_to_realloc->addr;
        }
        else if(all_blocks_list.IsNextFree(md_to_realloc) && md_to_realloc->size+md_to_realloc->list_next->size-AlignSizeToEight(sizeof(MallocMetadata)) > size)
        {
            md_to_realloc=Merge(md_to_realloc,false,true);
            if((long)(md_to_realloc->size-real_size-AlignSizeToEight(sizeof(MallocMetadata)))>=MIN_SPLIT_SIZE)
                Split(md_to_realloc, real_size);
            memmove(md_to_realloc->addr,oldp,old_size);
            return md_to_realloc->addr;
        }
        else if(all_blocks_list.IsPrevFree(md_to_realloc) && all_blocks_list.IsNextFree(md_to_realloc) 
                && md_to_realloc->size+md_to_realloc->list_next->size+md_to_realloc->list_prev->size-AlignSizeToEight(sizeof(MallocMetadata)) > size)
        {
            md_to_realloc=Merge(md_to_realloc->list_prev,false,false);
            md_to_realloc=Merge(md_to_realloc,false,true);
            if((long)(md_to_realloc->size-real_size-AlignSizeToEight(sizeof(MallocMetadata)))>=MIN_SPLIT_SIZE)
                Split(md_to_realloc, real_size);
            memmove(md_to_realloc->addr,oldp,old_size);
            return md_to_realloc->addr;
        }

        if (all_blocks_list.isLast(md_to_realloc))//Wildrness on itself
        {
            if(sbrk(real_size - md_to_realloc->size)==(void*)-1)
                return NULL;
            md_to_realloc->size=real_size;
            return oldp;
        }
        void* ptr = BlockAllocate(size);
        if (!ptr)
            return NULL;
        else
        {
            memmove(ptr,oldp,old_size);
            FreeBlock(oldp);
            return ptr;
        }
    }


    size_t numFreeBlocks()
    {
        size_t counter=0;
        for(int i=0;i<NUM_OF_BINS;i++)
            counter+=histogram[i].numFreeBlocksList();
        return counter;
    }

    size_t numFreeBytes()
    {
        size_t counter=0;
        for(int i=0;i<NUM_OF_BINS;i++)
            counter+=histogram[i].numFreeBytesList();
        return counter;
    }

    size_t numAllocatedBlocks()
    {
        return all_blocks_list.numAllocatedBlocksList()+mmap_allocated_blocks;
    }

    size_t numAllocatedBytes()
    {
        return all_blocks_list.numAllocatedBytesList()+mmap_allocated_bytes;
    }

    size_t numMetaDataBytes()
    {
        return all_blocks_list.numMetaDataBytesList()+mmap_allocated_blocks*AlignSizeToEight(sizeof(MallocMetadata));
    }

    size_t numMetaData()
    {
        return AlignSizeToEight(sizeof(MallocMetadata));
    }
        
};


void* smalloc(size_t size)
{
    if(size==0 || size>MAX_MALLOC_SIZE)
        return NULL;
    size=AlignSizeToEight(size);
    return sBlockManager->BlockAllocate(size);
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
    sBlockManager->FreeBlock(p);
}

void* srealloc(void* oldp, size_t size)
{
    if(size==0 || size>MAX_MALLOC_SIZE)
        return NULL;
    if(oldp==NULL)
        return smalloc(size);
    size=AlignSizeToEight(size);
    return sBlockManager->Rellocate(oldp,size);
}


size_t _num_free_blocks()
{
    return sBlockManager->numFreeBlocks();
}

size_t _num_free_bytes()
{
    return sBlockManager->numFreeBytes();
}

size_t _num_allocated_blocks()
{
    return sBlockManager->numAllocatedBlocks();
}

size_t _num_allocated_bytes()
{
    return sBlockManager->numAllocatedBytes();    
}

size_t _num_meta_data_bytes()
{
    return sBlockManager->numMetaDataBytes();    
}

size_t _size_meta_data()
{
    return sBlockManager->numMetaData();    
}
