#include <iostream>
#include <string>
#include <fstream>
#include <stdint.h>
#include <vector>

using namespace std;

struct sdrdefs {
	uint32_t devindex;
	string   serial;
};

int main() {

	string ln;
	ifstream cfgfile("ac.cfg");
	std::vector<sdrdefs> v;

	while(getline(cfgfile,ln)){
		sdrdefs d;
		if (ln[0]=='#')
			continue;
		else{
			std::size_t st,end;
			std::string::size_type sz;
			string ids = ln.substr(0,2);
			
			if ((ids[0]=='R')||(ids[1]=='R'))
				d.devindex = 0;
			else
				d.devindex = std::stoi(ids,&sz);
			
			st = ln.find(":");
			st = ln.find("'",st+1);
			end= ln.find("'",st+1);
			d.serial = ln.substr(st+1,end-st-1);
			v.push_back(d);
			}
		
	}
	cfgfile.close();

	//for (auto it = v.begin() ; it!=v.end(); it++) {
	cout << "size of vector:"<<endl;
	printf("%d\n", v.size());
	for (auto n : v){
		cout << n.serial;
		cout << ',';
		cout << n.devindex<<endl;
	}
}