#ifndef PACKETIZEH
#define PACKETIZEH
//#include <stdint.h>
#include <zmq.hpp>
#include <string>
#include <mutex>
#include <atomic>

struct hdr0{
	uint32_t globalseqn;
	uint32_t N;
	uint32_t L;
	uint32_t unused;
};

class cpacketize{
	std::atomic<int>	written;
	uint32_t 			globalseqn;
	uint32_t 			nchannels;
	uint32_t 			blocksize;
	bool 				noheader;
	size_t	 			packetlength;
	zmq::context_t 		context;
	zmq::socket_t  		socket;

	std::mutex bmutex;

	int8_t *packetbuf0, *packetbuf1;
public:
	cpacketize(uint32_t N,uint32_t L, std::string address, bool no_header) : context(1), socket(context,ZMQ_PUB) {
		globalseqn	= 0;
		nchannels	= N;
		blocksize	= L;
		noheader 	= no_header;
		socket.bind(address);

		if (noheader)
			packetlength = 2*nchannels*blocksize;
		else
			packetlength = (16 + 4*nchannels) + 2*nchannels*blocksize;

		packetbuf0	= new int8_t[packetlength];
		packetbuf1	= new int8_t[packetlength];
	}

	~cpacketize(){
		delete [] packetbuf0;
		delete [] packetbuf1;
	}

	int send(){
		if (!noheader){
			//fill static header. block readcounts filled by calls to write:
			hdr0 *hdr 		= (hdr0 *) packetbuf0;
			hdr->globalseqn	= globalseqn++;
			hdr->N 			= nchannels;
			hdr->L 			= blocksize;
			hdr->unused 	= 0;
		}
		
		std::lock_guard<std::mutex> lock(bmutex);
		int8_t *tmp=packetbuf0;
		packetbuf0=packetbuf1;
		packetbuf1=tmp;

		socket.send(packetbuf0,packetlength,0);
	}

	//the idea is that each thread could call this method from it's own context, as we're writing to separate locations
	int write(uint32_t channeln,uint32_t readcnt,int8_t *rp){

		std::lock_guard<std::mutex> lock(bmutex);
		uint32_t loc;

		if (!noheader){
			//fill dynamic size part of header, write readcounts
			*(packetbuf0 + sizeof(hdr0)+channeln)=readcnt;
			loc = (sizeof(hdr0)+nchannels*sizeof(uint32_t)) + channeln*blocksize;
		}
		else{
			loc = channeln*blocksize;
		}

		//copy data
		memcpy((int8_t *) (packetbuf0+loc),rp,blocksize);
		written++;
	}


};


#endif
