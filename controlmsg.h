#ifndef CONTROMSGH
#define CONTROLMSGH
//#include <stdint.h>
#include <zmq.hpp>
#include <string>

const uint32_t CONTROLMSG_MAGIC=(('C'<<24) & ('T'<<16) & ('R'<<8) & 'L');
const uint32_t CONTROLMSG_SETFCENTER=0x01;
const uint32_t CONTROLMSG_QUERYLAGS =0x02;

class ccontrolmsg{
	zmq::context_t 		context;
	zmq::socket_t  		socket;
	zmq::message_t 		message;

	float				*lags;
	uint32_t			nchannels;

public:
	ccontrolmsg(uint32_t N,std::string address) : context(1), socket(context,ZMQ_ROUTER) {
		nchannels = N;
		lags 	  = new float[nchannels];
		socket.bind(address);
	}

	~ccontrolmsg(){
		delete [] lags;
	}

	int setlag(uint32_t n, float dk)
	{
		if ((n>=0) & (n<nchannels))
			lags[n]=dk;
	}

	uint32_t listen(){
		size_t 		len;
		uint32_t	*msg;
		uint32_t 	ret=0;

		socket.recv(&message,ZMQ_NOBLOCK);
		msg = static_cast<uint32_t*>(message.data());
		len = message.size()/sizeof(uint32_t);

		if (len>2)
			if (msg[0]==CONTROLMSG_MAGIC){
				switch (msg[1]) {
					case CONTROLMSG_SETFCENTER:
						fprintf(stderr,"\nControl message received, tuning all receivers to %2.2f MHz\n\n",msg[2]/1e6);
						ret=msg[2];
					break;
					case CONTROLMSG_QUERYLAGS:
					{
						zmq::message_t reply(nchannels*sizeof(float));
						std::memcpy(message.data(),lags,nchannels*sizeof(float));
					}
					break;
					default:
					break;
				}

			}
	return ret;
	}

};


#endif
