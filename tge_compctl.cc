#if HAVE_CONFIG
 #include "config.h"
#endif

#include <iostream>
#include <stdio.h>
#include <string.h>
#include <fstream>
#include <unistd.h>
#include "tge_compctl.h"

using namespace std;

// Compressing orders format:
//
//   # hogehoge      comment
//   0               set flag to zero
//   1               set flag to one
//   *[condition]    check condition (AND)
//   |[condition]    check condition (OR)
//   -[order]        immediately do as order
//
//
// Condition format:
//
//   D/home/someone/hugedir      below the specified directory
//   !D/home/someone/hugedir     not below the specified directory
//   Etxt                        extension is .txt
//   !Etxt                       extention is not .txt
//
//
// Order format:
//
//   U            uncompressed
//   L            compress by LZOx1
//
//
// Example:
//
//   # txt/xml/eps files under /home/someone to be compressed by LZO,
//   # while others are not compressed
//   0
//   |txt
//   |xml
//   |eps
//   D/home/someone
//   -L

void CompressionControl::init(const char* homedir)
{
  compressingOrders.clear();
  compressingOrders.push_back("1");

  string filename = homedir;
  filename += "/.tge/.tgefs";
  static const uid_t ROOT = 0;
  if(getuid() == ROOT) {
    filename = "/etc/tgefscc.conf";
  }

  ifstream ist(filename.c_str());
  if(!ist) {
    cerr << "Could not open '" << filename << "'. Use defaults" << endl;
  } else {
    int lineCount = 0;
    string line;
    while(getline(ist, line)) {
      ++lineCount;
      if(line.empty()) // skip empty line
	continue;
      switch(line[0]) {
      case '#':
	// comment
	break;
      case '0':
      case '1':
	if(line.size() != 1) {
	  cerr << "ERROR: 0/1 do not take arguments. at line " << lineCount << endl;
	  break;
	}
	compressingOrders.push_back(line);
	break;
      case '*':
      case '|':
	if(line.size() <= 1) {
	  cerr << "ERROR: no condition at line " << lineCount << endl;
	  break;
	}
	if(line[1] == 'D') {
	  if(3 <= line.size() && line[2] == '/') {
	    if(line[line.size() - 1] != '/') {
	      compressingOrders.push_back(line + "/");
	    } else {
	      compressingOrders.push_back(line);
	    }
	  } else {
	    cerr << "ERROR: D condition takes an absolute path only. at line " << lineCount << endl;
	    break;
	  }
	} else if(line[1] == 'E') {
	  compressingOrders.push_back(line);
	} else if(line[1] == '!') {
	  if(3 <= line.size() && (line[2] == 'D' || line[2] == 'E')) {
	    if(line[2] == 'D') {
	      if(4 <= line.size() && line[3] == '/') {
		if(line[line.size() - 1] != '/') {
		  compressingOrders.push_back(line + "/");
		} else {
		  compressingOrders.push_back(line);
		}
	      } else {
		cerr << "ERROR: !D condition takes an absolute path only. at line " << lineCount << endl;
		break;
	      }
	    } else if(line[2] == 'E') {
	      compressingOrders.push_back(line);
	    } else {
	      // this should never happen
	    }
	  } else {
	    cerr << "ERROR: negation condition must be followed by a predicate. at line " << lineCount << endl;
	    break;
	  }
	} else {
	  cerr << "ERROR: unknown condition at line " << lineCount << endl;
	}
	break;
      case '-':
	if(line.size() < 2) {
	  cerr << "ERROR: no compression type at line " << lineCount << endl;
	  break;
	}
	if(line[1] == 'U' || line[1] == 'L') {
	  compressingOrders.push_back(line);
	} else {
	  cerr << "ERROR: unknown compression type '" << line.substr(1) << "' at line " << lineCount << endl;
	  break;
	}
	break;
      default:
	cerr << "ERROR: unknown order at line " << lineCount << endl;
	break;
      }
    }
  }
  // sets the default
  compressingOrders.push_back("1");
  compressingOrders.push_back("-U");
}

int CompressionControl::predicate(const string& expr, int cursor, const char* path)
{
  int true_val  = 1;
  int false_val = 0;
  if((int)expr.size() <= cursor)
    return 0;
  if(expr[cursor] == '!') {
    true_val  = 0;
    false_val = 1;
    cursor++;
  }
  if(expr[cursor] == 'D') {
    cursor++;
    const char* prefix = expr.c_str() + cursor;
    // note that expr ends with '/'.
    if(strncmp(prefix, path, expr.size() - cursor) == 0)
      return true_val;
    return false_val;
  } else if(expr[cursor] == 'E') {
    cursor++;
    const char* ext    = expr.c_str() + cursor;
    int i = strlen(path) - 1;
    while(0 <= i && path[i] != '/' && path[i] != '.')
      i--;
    if(0 <= i) {
      if(path[i] == '/') {
	// no ext
	if(strlen(ext) == 0)
	  return true_val;
	return false_val;
      } else if(path[i] == '.') {
	if(strcmp(path + i + 1, ext) == 0)
	  return true_val;
	return false_val;
      }
    } else {
      // path didn't contain '/' nor '.'
      // this shouldn't be observed unless FUSE is buggy
    }
  } else {
    // this should never occur.
    return 0;
  }
  return 0;
}

CompressionControl::CompressionType CompressionControl::getCompressionType(const char* path)
{
  int currentFlag = 0;

  for(vector<string>::const_iterator cit = compressingOrders.begin(); cit != compressingOrders.end(); ++cit) {
    const string& l = *cit;
    if(l.empty()) continue; // this should never happen, though...
    switch(l[0]) {
    case '0':
      currentFlag = 0;
      break;
    case '1':
      currentFlag = 1;
      break;
    case '-':
      if(currentFlag == 1) {
	if(l.size() <= 1)
	  return Uncompressed; // this should never happen
	if(l[1] == 'L') return LZOx1;
	if(l[1] == 'U') return Uncompressed;
	return Uncompressed; // this should never happen
      }
      break;
    case '*':
      if(currentFlag == 1) {
	currentFlag = predicate(l, 1, path);
      }
      break;
    case '|':
      if(currentFlag == 0) {
	currentFlag = predicate(l, 1, path);
      }
      break;
    default:
      // this should never happen
      ;
    }
  }
  return Uncompressed;
}

/*
int main(int argc, char**argv)
{
  CompressionControl cc;
  cc.init("/home/mkasa");
  string l;
  while(getline(cin, l)) {
    if(l.empty())
      break;
    CompressionControl::CompressionType t = cc.getCompressionType(l.c_str());
    cout << "type = " << t << endl;
  }
}
*/
