#include <stdio.h>
#include <string.h>
#include <stdint.h>

//command-line parsing, getopt:
#include "getopt/getopt.h"
#include "controlmsg.h"


void usage(void)
{
	fprintf(stderr,
		"\ncontrolcmd: send control messages to coherentsdr\n\n"
		"Usage:\n"
		"\t[-f frequency_to_tune_to [Hz'(default 480MHz)]\n"
		"\t[-a address of control socket (default: tcp://127.0.0.1:5556)]\n");
	exit(1);
}

int main(int argc, char **argv){

	int opt;
	uint32_t fcenter=480000000;
	std::string address="tcp://127.0.0.1:5556";

	while ((opt = getopt(argc, argv, "f:h:a:")) != -1) {
		switch (opt) {
		case 'f':
			fcenter=(uint32_t) atof(optarg);
			break;
		case 'h':
			usage();
			break;
		case 'a':
			address=std::string(optarg);
			break;
		default:
			usage();
			break;
		}
	}
	//if (argc <= optind)
	//	usage();


	if (fcenter!=0)
	{
		zmq::context_t context(1);
    	zmq::socket_t socket(context, ZMQ_DEALER);

    	zmq::message_t message(sizeof(uint32_t)*3);
    	uint32_t *msg =  static_cast<uint32_t*>(message.data());

    	msg[0]=CONTROLMSG_MAGIC;
    	msg[1]=CONTROLMSG_SETFCENTER;
    	msg[2]=fcenter;

    	fprintf(stderr,"sending to: %s \n",address.data());
    	socket.connect(address.data());
    	socket.send(message);

	}

}