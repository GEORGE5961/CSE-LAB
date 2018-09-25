// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

yfs_client::yfs_client()
{
    ec = new extent_client();

}

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
    ec = new extent_client();
    if (ec->put(1, "") != extent_protocol::OK)
        printf("error init root dir\n"); // XYB: init root dir
}

yfs_client::inum
yfs_client::n2i(std::string n)
{
    std::istringstream ist(n);
    unsigned long long finum;
    ist >> finum;
    return finum;
}

std::string
yfs_client::filename(inum inum)
{
    std::ostringstream ost;
    ost << inum;
    return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
    extent_protocol::attr a;

    if (ec->getattr(inum, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        return false;
    }

    if (a.type == extent_protocol::T_FILE) {
        printf("isfile: %lld is a file\n", inum);
        return true;
    } 
    printf("isfile: %lld is a dir\n", inum);
    return false;
}
/** Your code here for Lab...
 * You may need to add routines such as
 * readlink, issymlink here to implement symbolic link.
 * 
 * */

bool
yfs_client::isdir(inum inum)
{
    // Oops! is this still correct when you implement symlink?
    return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
    int r = OK;

    printf("getfile %016llx\n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }

    fin.atime = a.atime;
    fin.mtime = a.mtime;
    fin.ctime = a.ctime;
    fin.size = a.size;
    printf("getfile %016llx -> sz %llu\n", inum, fin.size);

release:
    return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
    int r = OK;

    printf("getdir %016llx\n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }
    din.atime = a.atime;
    din.mtime = a.mtime;
    din.ctime = a.ctime;

release:
    return r;
}


#define EXT_RPC(xx) do { \
    if ((xx) != extent_protocol::OK) { \
        printf("EXT_RPC Error: %s:%d \n", __FILE__, __LINE__); \
        r = IOERR; \
        goto release; \
    } \
} while (0)

// Only support set size of attr
int
yfs_client::setattr(inum ino, size_t size)
{
    int r = OK;

    /*
     * your code goes here.
     * note: get the content of inode ino, and modify its content
     * according to the size (<, =, or >) content length.
     */
    extent_protocol::attr a;
    if (ec->getattr(ino, a) != extent_protocol::OK) {
        return IOERR;
    }
    std::string buf;
    char *new_char=new char[size];
    if(ec->get(ino, buf) != extent_protocol::OK) {
        return IOERR;
    }
    size_t copy_size = size<a.size ? size:a.size;
    memcpy(new_char,buf.c_str(), copy_size);
    std::string newbuf(new_char);
    if(ec->put(ino,newbuf)!=extent_protocol::OK){
        free(new_char);
        return IOERR;
    }
	free(new_char);
    return r;
}

int
yfs_client::create(inum parent, const char *name, mode_t mode, inum &ino_out)
{
    int r = OK;

    /*
     * your code goes here.
     * note: lookup is what you need to check if file exist;
     * after create file or dir, you must remember to modify the parent infomation.
     */
    bool found=false;
	inum fir_inum=0;
	if(lookup(parent,name,found,fir_inum)!=extent_protocol::OK) {
        return IOERR;
    }
	if(found){
		return EXIST;
	}
	std::string buf;
	if(ec->get(parent, buf) != extent_protocol::OK) {
        return IOERR;
    }
	if(ec->create(extent_protocol::T_FILE,ino_out)!=extent_protocol::OK){
		return IOERR;
	}
	buf += name;
	buf += './';
    std::stringstream st;
	std::string str;
	st<<ino_out;
	st>>str;
    buf+= str;
	buf+='./';
	if(ec->put(parent,buf)!=extent_protocol::OK){
		r = IOERR;
	}
	return r;
}

int
yfs_client::mkdir(inum parent, const char *name, mode_t mode, inum &ino_out)
{
    int r = OK;

    /*
     * your code goes here.
     * note: lookup is what you need to check if directory exist;
     * after create file or dir, you must remember to modify the parent infomation.
     */
    bool found=false;
	inum fir_inum=0;
	if(lookup(parent,name,found,fir_inum)!=extent_protocol::OK) {
        return IOERR;
    }
	if(found){
		return EXIST;
	}
	std::string buf;
	if(ec->get(parent, buf) != extent_protocol::OK) {
        return IOERR;
    }
	if(ec->create(extent_protocol::T_DIR,ino_out)!=extent_protocol::OK){
		return IOERR;
	}
	buf+=name;
	buf+='./';
	std::stringstream st;
	std::string str;
	st<<ino_out;
	st>>str;
    buf+= str;
	buf+='./';
	if(ec->put(parent,buf)!=extent_protocol::OK){
		return IOERR;
	}
	return r;
}

int
yfs_client::lookup(inum parent, const char *name, bool &found, inum &ino_out)
{
    int r = OK;

    /*
     * your code goes here.
     * note: lookup file from parent dir according to name;
     * you should design the format of directory content.
     */
    found = false;
    std::string buf;
	if(ec->get(parent, buf) != extent_protocol::OK) {
        return IOERR;
    }
	std::list<dirent> dir_list = str2list(buf);
    std::list<dirent>::iterator it;
	for (it=dir_list.begin(); it!=dir_list.end(); ++it){
		if(it->name==name){
			found=true;
			ino_out=it->inum;
			return r;
		}
	}
    return r;
}

int
yfs_client::readdir(inum dir, std::list<dirent> &list)
{
    int r = OK;

    /*
     * your code goes here.
     * note: you should parse the dirctory content using your defined format,
     * and push the dirents to the list.
     */
    std::string buf;
	if (ec->get(dir, buf) != extent_protocol::OK) {
        return IOERR;
    }
    std::list<dirent> dir_list=str2list(buf);
    std::list<dirent>::iterator it;
    for (it=dir_list.begin(); it!=dir_list.end(); ++it)
    {
        list.push_back(*it);
    }
    return r;
}

int
yfs_client::read(inum ino, size_t size, off_t off, std::string &data)
{
    int r = OK;

    /*
     * your code goes here.
     * note: read using ec->get().
     */
    ec->get(ino, data);
    if (off <= data.size())
    {
        if (off + size <= data.size())
        {
            data = data.substr(off, size);
        } 
        else
        {
            uint len = data.size();
            data = data.substr(off, len - off);
        }
    }
    return r;
}

int
yfs_client::write(inum ino, size_t size, off_t off, const char *data,
        size_t &bytes_written)
{
    int r = OK;

    /*
     * your code goes here.
     * note: write using ec->put().
     * when off > length of original file, fill the holes with '\0'.
     */
    std::string buf;
	if (ec->get(ino, buf) != extent_protocol::OK) {
        	r = IOERR;
        	return r;
    	}
	if(off+size>buf.size()){
		buf.resize(off+size,'\0');
	}
	buf.replace(off,size,data,size);
	if(off<=buf.size()){
		bytes_written=size;
	}
	else{
		bytes_written=off-buf.size()+size;
	}			
	if (ec->put(ino, buf) != extent_protocol::OK) {
        	r = IOERR;
        	return r;
    }
    return r;
}

int yfs_client::unlink(inum parent,const char *name)
{
    int r = OK;

    /*
     * your code goes here.
     * note: you should remove the file using ec->remove,
     * and update the parent directory content.
     */
    std::string buf;
	if(ec->get(parent, buf) != extent_protocol::OK) {
        return IOERR;
    }

	std::list<dirent> dir_list=str2list(buf);
	std::list<dirent>::iterator it;
    for (it=dir_list.begin(); it!=dir_list.end(); ++it){
		if(std::string(name)==it->name)
			break;
	}
	
	if(it==dir_list.end()){
		return r;
	}
	
	if(ec->remove(it->inum)!=extent_protocol::OK) {
        return IOERR;
    }
	dir_list.erase(it);
	buf.clear();
	for (it=dir_list.begin(); it!=dir_list.end(); ++it){
		buf+=it->name;
		buf+='./';
        std::stringstream st;
	    std::string str;
	    st<<(it->inum);
	    st>>str;
        buf+= str;
		buf+='./';
	}
	if(ec->put(parent, buf) != extent_protocol::OK) {
        return IOERR;
    }
    return r;
}

std::list<yfs_client::dirent> yfs_client::str2list(const std::string &str){

    std::vector<std::string> dir_vec;
    std::list<yfs_client::dirent> rst;
    size_t mark = 0;
	size_t pos =str.find_first_of('/', mark);
    while(pos!=std::string::npos){
        dir_vec.push_back(str.substr(mark,pos-mark));
        mark = pos+1;
        pos = str.find_first_of('./',mark);
	}
    if(mark < str.size()){
		dir_vec.push_back(str.substr(mark,str.size()-mark));
	}
    for(int i=0;i<dir_vec.size();i+=2){
		yfs_client::dirent tmpdir;
		if(i==dir_vec.size()-1){
			break;
		}
		tmpdir.name=dir_vec[i];
		tmpdir.inum=n2i(dir_vec[i+1]);
		rst.push_back(tmpdir);
	}
	return rst;
}
