#if HAVE_CONFIG
 #include "config.h"
#endif

#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string.h>
#include <vector>
#include <unistd.h>
#include <limits.h>

using namespace std;

static char config_file_name[] = "/.tge/.tgerc";
char cacheDirectoryRoot[PATH_MAX + PATH_MAX] = "/";
char tgeLockdServer[1024];
int  tgeLockdPort;
int  minimumFileSizeToEnableLock = 50000000;

vector<string> splitBySpace(const string& origstr)
{
  vector<string> retval;
  int i = 0;
  while(i < (int)origstr.size()) {
    // skip spaces
    while(i < (int)origstr.size() && origstr[i] == ' ')
      i++;
    if(!(i < (int)origstr.size()))
      break; // end of line
    int ncur = i;
    while(ncur < (int)origstr.size() && origstr[i] != ' ')
      ncur++;
    retval.push_back(origstr.substr(i, ncur - i));
    i = ncur;
  }
  return retval;
}

string expandEnvironmentVariable(const string& origstr)
{
  string retval;
  for(int i = 0; i < (int)origstr.size(); i++) {
    if(origstr[i] != '$' || !(i + 1 < (int)origstr.size()) || origstr[i + 1] != '{') {
      retval += origstr[i];
      continue;
    }
    const int varname_start = i + 2;
    int varname_end = varname_start;
    while(varname_end < (int)origstr.size() && origstr[varname_end] != '}')
      varname_end++;
    if(!(varname_end < (int)origstr.size())) {
      cerr << "Unmatched parenthesis.\n" << origstr << endl;
      retval += origstr[i];
      continue;
    }
    const char *value = getenv(origstr.substr(varname_start, varname_end - varname_start).c_str());
    if(value != NULL) {
      retval += value;
    }
    i = varname_end;
  }
  return retval;
}

bool load_application_config() 
{
  const char* homeenv = getenv("HOME");
  if(homeenv == NULL) {
    cerr << "ERROR: HOME environment variable is not set" << endl;
    return false;
  }
  string homePath = homeenv;
  string appConfigFile = homePath + config_file_name;
  static const uid_t ROOT_USER = 0;
  if(getuid() == ROOT_USER) {
    appConfigFile = "/etc/tgefs.conf";
  }
  ifstream ist(appConfigFile.c_str());
  if(!ist) {
    cerr << "WARNING: could not open '" << appConfigFile << "'\n";
    cerr << "         All the settings will be default.\n";
    return false;
  }
  string line;
  int lineCount = 0;
  while(getline(ist, line)) {
    ++lineCount;
    if(line.empty() || line[0] == '#')
      continue;
    const string::size_type p = line.find('=');
    if(p == string::npos) {
      cerr << "WARNING: syntax error at line " << lineCount << " in " << appConfigFile << endl;
      continue;
    }
    const string leftHand = line.substr(0, p);
    const string rightHand = line.substr(p + 1);
    if(leftHand == "server") {
      strncpy(tgeLockdServer, rightHand.c_str(), sizeof(tgeLockdServer) - 1);
      tgeLockdServer[sizeof(tgeLockdServer) - 1] = '\0';
    } else if(leftHand == "port") {
      tgeLockdPort = std::atoi(rightHand.c_str());
    } else if(leftHand == "locksize") {
      minimumFileSizeToEnableLock = std::atoi(rightHand.c_str());
    } else if(leftHand == "localdisk") {
      // currently, we have nothing to do here
    } else if(leftHand == "tgelocaldisk") {
      char hostname[HOST_NAME_MAX + 1];
      {
	const int result = gethostname(hostname, sizeof(hostname) - 1);
	hostname[sizeof(hostname) - 1] = '\0';
	if(result == -1) {
	  cerr << "Could not get hostname" << endl;
	  return false;
	}
      }
      vector<string> localDiskSpecifiers = splitBySpace(rightHand);
      for(int i = 0; i < (int)localDiskSpecifiers.size(); i++) {
	const string& lds = localDiskSpecifiers[i];
	// cerr << "LDS[" << i << "] = " << lds << endl;
	const string::size_type p = lds.find(':');
	if(p == string::npos || lds.substr(0, p) == "*" || lds.substr(0, p) == hostname) {
	  strncpy(cacheDirectoryRoot, expandEnvironmentVariable(p == string::npos ? lds : lds.substr(p+1)).c_str(), sizeof(cacheDirectoryRoot) - 1);
	  cacheDirectoryRoot[sizeof(cacheDirectoryRoot) - 1] = '\0';
	  break;
	}
      }
    } else {
      cerr << "WARNING: unknown variable '" << leftHand << "'" << endl;
    }
  }
  return true;
}

