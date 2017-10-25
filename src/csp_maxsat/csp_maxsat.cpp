/*******************************************************************************
* Author: Ehsan Haghshenas
* Last update: Oct 19, 2017
*******************************************************************************/

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <getopt.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>

using namespace std;

#define MAX_CELL 300
#define MAX_MUT 200

string  par_inputFile = "";
string  par_outDir = "";
int     par_fnWeight = -1;
int     par_fpWeight = -1;
int     par_maxColRemove = 0;
int     par_threads = 1;
bool    IS_PWCNF = true;
string  MAX_SOLVER = "openwbo";
string  MAX_EXE = "";

int mat[MAX_CELL][MAX_MUT]; // the 0/1 matrix
vector<string> cellId;
vector<string> mutId;
int var_x[MAX_CELL][MAX_MUT]; // X variables for the maxSAT
int var_y[MAX_CELL][MAX_MUT]; // Y variables for the maxSAT; if(Iij==0) Yij=Xij and if(Iij==1) Yij=~Xij
int weight_x[MAX_CELL][MAX_MUT]; // weight of X variables
int var_b[MAX_MUT][MAX_MUT][2][2];
int var_k[MAX_MUT];
pair<int, int> map_y2ij[MAX_CELL * MAX_MUT + 10]; // maps Y variables to matrix position (row and column)
vector<string> clauseSoft; // the set of soft clauses for wcnf formulation
vector<string> clauseHard; // the set of soft clauses for wcnf formulation

int numMut; // actual number of mutations (columns)
int numCell; // actual number of cells (rows)
int numVarY; // number of Y variables
int numVarX; // number of X variables
int numVarB; // number of B variables
int numVarK; // number of K variables
int numZero; // number of zeros in the input matrix
int numOne; // number of ones in the input matrix
int numTwo; // number of twos in the input matrix

string int2str(int n)
{
    ostringstream sout;
    sout<< n;
    return sout.str();
}

int str2int(string s)
{
    int retVal;
    istringstream sin(s.c_str());
    sin >> retVal;
    return retVal;
}

void print_usage()
{
    cout<< endl
        << "usage: csp_maxsat [-h] -f FILE -n FNWEIGHT -p FPWEIGHT -o OUTDIR" << endl
        << "                  [-m MAXMUT] [-t THREADS]" << endl;
}

void print_help()
{
    cout<< endl
        << "Required arguments:" << endl
        << "   -f, --file     STR        Input matrix file" << endl
        << "   -n, --fnWeight INT        Weight for false negative" << endl
        << "   -p, --fpWeight INT        Weight for false negative" << endl
        << "   -o, --outDir   STR        Output directory" << endl
        << "   -s, --solver   STR        Path to the Max-SAT solver" << endl
        << endl
        << "Optional arguments:" << endl
        << "   -m, --maxMut   INT        Max number mutations to be eliminated [0]" << endl
        << "   -t, --threads  INT        Number of threads [1]" << endl
        << endl
        << "Other arguments:" << endl
        << "   -h, --help                Show this help message and exit" << endl;
}

bool command_line_parser(int argc, char *argv[])
{
    int index;
    char c;
    static struct option longOptions[] = 
    {
    //     {"progress",                no_argument,        &progressRep,       1},
        {"file",                   required_argument,  0,                  'f'},
        {"fnWeight",               required_argument,  0,                  'n'},
        {"fnWeight",               required_argument,  0,                  'p'},
        {"outDir",                 required_argument,  0,                  'o'},
        {"maxMut",                 required_argument,  0,                  'm'},
        {"threads",                required_argument,  0,                  't'},
        {"solver",                 required_argument,  0,                  's'},
        {"help",                   no_argument,        0,                  'h'},
        {0,0,0,0}
    };

    while ( (c = getopt_long ( argc, argv, "f:n:p:o:m:t:s:h", longOptions, &index))!= -1 )
    {
        switch (c)
        {
            case 'f':
                par_inputFile = optarg;
                break;
            case 'n':
                par_fnWeight = str2int(optarg);
                if(par_fnWeight < 1)
                {
                    cerr<< "[ERROR] Weight for false negative should be an integer >= 1" << endl;
                    return false;
                }
                break;
            case 'p':
                par_fpWeight = str2int(optarg);
                if(par_fpWeight < 1)
                {
                    cerr<< "[ERROR] Weight for false positive should be an integer >= 1" << endl;
                    return false;
                }
                break;
            case 'o':
                par_outDir = optarg;
                break;
            case 'm':
                par_maxColRemove = str2int(optarg);
                if(par_maxColRemove < 0)
                {
                    cerr<< "[ERROR] Maximum number of mutation removal should be an integer >= 0" << endl;
                    return false;
                }
                break;
            case 't':
                par_threads = str2int(optarg);
                if(par_threads != 1)
                {
                    cerr<< "[ERROR] Only single thread is supported at the moment!" << endl;
                    return false;
                }
                break;
            case 's':
                MAX_EXE = optarg;
                break;
            case 'h':
                print_usage();
                print_help();
                exit(EXIT_SUCCESS);
        }
    }

    if(par_inputFile == "")
    {
        cerr<< "[ERROR] option -f/--file is required" << endl;
        print_usage();
        return false;
    }

    if(par_outDir == "")
    {
        cerr<< "[ERROR] option -o/--outDir is required" << endl;
        print_usage();
        return false;
    }

    if(par_fnWeight < 0)
    {
        cerr<< "[ERROR] option -n/--fnWeight is required" << endl;
        print_usage();
        return false;
    }

    if(par_fpWeight < 0)
    {
        cerr<< "[ERROR] option -p/--fpWeight is required" << endl;
        print_usage();
        return false;
    }

    if(MAX_EXE == "")
    {
        cerr<< "[ERROR] option -s/--solver is required" << endl;
        print_usage();
        return false;	
    }

    return true;
}

void get_input_data(string path)
{
	int i, j;
	string tmpStr;
	string line;
    ifstream fin(path.c_str());
    if(fin.is_open() == false)
    {
        cerr<< "Could not open file: " << path << endl;
        exit(EXIT_FAILURE);
    }
	// process the header
    getline(fin, line);
    istringstream sin1(line);
    while(sin1 >> tmpStr)
    {
        mutId.push_back(tmpStr);
    }
    numMut = mutId.size() - 1;
    //
	i = 0;
	while(getline(fin, line))
	{
		istringstream sin(line);
        sin >> tmpStr; // cell name
        cellId.push_back(tmpStr);
		for(int j = 0; j < numMut; j++)
		{
			sin >> mat[i][j];
		}
		i++;
	}
    numCell = i;
    fin.close();
}

void set_y_variables()
{
	int i, j;
	numVarY = 0;

	for(i = 0; i < numCell; i++)
	{
		for(j = 0; j < numMut; j++)
		{
			numVarY++;
			var_y[i][j] = numVarY;
			map_y2ij[numVarY] = make_pair<int, int>(i, j);
		}
	}
}

void set_x_variables()
{
	int i, j;
	numVarX = 0;

	for(i = 0; i < numCell; i++)
	{
		for(j = 0; j < numMut; j++)
		{
			numVarX++;
			var_x[i][j] = numVarY + numVarX;
		}
	}
}

void set_b_variables()
{
    int i, j, p, q;
    numVarB = 0;

    for(p = 0; p < numMut; p++)
    {
        for(q = 0; q < numMut; q++)
        {
            for(i = 0; i < 2; i++)
            {
                for(j = 0; j < 2; j++)
                {
                    numVarB++;
                    var_b[p][q][i][j] = numVarY + numVarX + numVarB;
                }
            }
        }
    }
}

void set_k_variables()
{
    int p;
    numVarK = 0;

    for(p = 0; p < numMut; p++)
    {
        numVarK++;
        var_k[p] = numVarY + numVarX + numVarB + numVarK;
    }
}

void add_variable_clauses()
{
	int i, j;
	numZero = 0;
	numOne = 0;
	numTwo = 0;

	string str_fnWeight = int2str(par_fnWeight);
	string str_fpWeight = int2str(par_fpWeight);

	for(i = 0; i < numCell; i++)
	{
		for(j = 0; j < numMut; j++)
		{
			// fout<< weight_x[map_x2ij[i].first][map_x2ij[i].second] << " " << -1*i << " 0\n";
			if(mat[i][j] == 0)
			{
				numZero++;
				clauseSoft.push_back(str_fnWeight + " " + int2str(-1*var_x[i][j]));
				clauseHard.push_back(int2str(-1*var_x[i][j]) + " " + int2str(var_y[i][j]));
				clauseHard.push_back(int2str(var_x[i][j]) + " " + int2str(-1*var_y[i][j]));
			}
			else if (mat[i][j] == 1)
			{
				numOne++;
				clauseSoft.push_back(str_fpWeight + " " + int2str(-1*var_x[i][j]));
				clauseHard.push_back(int2str(var_x[i][j]) + " " + int2str(var_y[i][j]));
				clauseHard.push_back(int2str(-1*var_x[i][j]) + " " + int2str(-1*var_y[i][j]));
			}
			else // mat[i][j] == 2 (not available)
			{
				numTwo++;
				clauseHard.push_back(int2str(-1*var_x[i][j]) + " " + int2str(var_y[i][j]));
				clauseHard.push_back(int2str(var_x[i][j]) + " " + int2str(-1*var_y[i][j]));
			}
		}
	}
}

void add_conflict_clauses()
{
	int i;
	int p, q;
	for(i = 0; i < numCell; i++)
	{
		for(p = 0; p < numMut; p++)
		{
			for(q = p; q < numMut; q++)
			{
				// ~Yip v ~Yiq v Bpq11
				clauseHard.push_back(int2str(-1*var_y[i][p]) + " " + int2str(-1*var_y[i][q]) + " " + int2str(var_b[p][q][1][1]));
				// Yip v ~Yiq v Bpq01
				clauseHard.push_back(int2str(var_y[i][p]) + " " + int2str(-1*var_y[i][q]) + " " + int2str(var_b[p][q][0][1]));
				// ~Yip v Yiq v Bpq10
				clauseHard.push_back(int2str(-1*var_y[i][p]) + " " + int2str(var_y[i][q]) + " " + int2str(var_b[p][q][1][0]));
                if(par_maxColRemove > 0) // column elimination enabled
                {
                    // Kp v Kq v ~Bpq01 v ~Bpq10 v ~Bpq11
                    clauseHard.push_back(int2str(var_k[p]) + " " + int2str(var_k[q]) + " " + int2str(-1*var_b[p][q][0][1]) + " " + int2str(-1*var_b[p][q][1][0]) + " " + int2str(-1*var_b[p][q][1][1]));
                }
                else // column elimination disabled
                {
                    // ~Bpq01 v ~Bpq10 v ~Bpq11
                    clauseHard.push_back(int2str(-1*var_b[p][q][0][1]) + " " + int2str(-1*var_b[p][q][1][0]) + " " + int2str(-1*var_b[p][q][1][1]));
                }
			}
		}
	}
}

int next_comb(int comb[], int k, int n)
{
    int i = k - 1;
    ++comb[i];
    while ((i >= 0) && (comb[i] >= n - k + 1 + i))
    {
        --i;
        ++comb[i];
    }

    if (comb[0] > n - k) /* Combination (n-k, n-k+1, ..., n) reached */
        return 0; /* No more combinations can be generated */

    /* comb now looks like (..., x, n, n, n, ..., n).
    Turn it into (..., x, x + 1, x + 2, ...) */
    for (i = i + 1; i < k; ++i)
        comb[i] = comb[i - 1] + 1;

    return 1;
}

void add_column_clauses()
{
    int i;
    // code for C(n, k) 
    // n choose k
    int n = numMut;
    int k = par_maxColRemove + 1;
    int comb[numMut + 10]; // comb[i] is the index of the i-th element in the combination
    for (i = 0; i < k; i++)
        comb[i] = i;

    do
    {
        string tmpClause = "";
        for(i = 0; i < k; i++)
            tmpClause += int2str(-1*var_k[comb[i]]) + " ";
        clauseHard.push_back(tmpClause);
    }while(next_comb(comb, k, n));
}

void write_maxsat_input(string path)
{
	int i, j;
	int hardWeight = numZero * par_fnWeight + numOne * par_fpWeight + 1;
	ofstream fout(path.c_str());
	if(fout.is_open() == false)
	{
        cerr<< "Could not open file: " << path << endl;
        exit(EXIT_FAILURE);
	}
	//
	if(IS_PWCNF)
	{
        fout<< "p wcnf " << numVarY + numVarX + numVarB + numVarK << " " << clauseSoft.size() + clauseHard.size() << " " << hardWeight << "\n";
	}
	else
	{
        fout<< "p wcnf " << numVarY + numVarX + numVarB + numVarK << " " << clauseSoft.size() + clauseHard.size() << "\n";
	}
	// soft clauses
	for(i = 0; i < clauseSoft.size(); i++)
	{
		fout<< clauseSoft[i] << " 0\n";
	}
	// hard clauses
	for(i = 0; i < clauseHard.size(); i++)
	{
		fout<< hardWeight << " " << clauseHard[i] << " 0\n";
	}

	fout.close();
}

bool read_maxsat_output(string path, int &flip, int &flip01, int &flip10, int &flip20, int &flip21)
{
    flip = 0;
    flip01 = 0;
    flip10 = 0;
    flip20 = 0;
    flip21 = 0;
    string line;
    bool oLine = false, sLine = false, vLine = false;
    ifstream fin(path.c_str());
    if(fin.is_open() == false)
    {
        cerr<< "Could not open file: " << path << endl;
        exit(EXIT_FAILURE);
    }
    // parse
    while(getline(fin, line))
    {
        if(line[0] == 'o')
        {
            oLine = true;
        }
        if(line[0] == 's')
        {
            sLine = true;
        }
        if(line[0] == 'v')
        {
            vLine = true;
            // update the input matrix
            int tmpVar, tmpVarAbs, oldVal;
            istringstream sin(line.substr(1));
            while(sin >> tmpVar)
            {
            	tmpVarAbs = abs(tmpVar);
                if(tmpVarAbs <= numVarY)
                {
                	oldVal = mat[map_y2ij[tmpVarAbs].first][map_y2ij[tmpVarAbs].second];

                	if(oldVal == 0)
                	{
                		if(tmpVar > 0)
                		{
                			flip++;
                			flip01++;
                			mat[map_y2ij[tmpVarAbs].first][map_y2ij[tmpVarAbs].second] = 1;
                		}
                	}
                	else if(oldVal == 1)
                	{
                		if(tmpVar < 0)
                		{
                			flip++;
                			flip10++;
                			mat[map_y2ij[tmpVarAbs].first][map_y2ij[tmpVarAbs].second] = 0;
                		}
                	}
                	else // oldVal == 2
                	{
                		if(tmpVar < 0)
                		{
                			flip++;
                			flip20++;
                			mat[map_y2ij[tmpVarAbs].first][map_y2ij[tmpVarAbs].second] = 0;
                		}
                		else // tmpVar > 0
                		{
                			flip++;
                			flip21++;
                			mat[map_y2ij[tmpVarAbs].first][map_y2ij[tmpVarAbs].second] = 1;
                		}
                	}
                }
            }
        }
    }
    fin.close();
    return (oLine && sLine && vLine);
}

void write_output_matrix(string path)
{
    int i, j;
    ofstream fout(path.c_str());
    // header
    for(i = 0; i < mutId.size(); i++)
    {
        fout<< mutId[i] << (i < mutId.size() - 1 ? "\t" : "");
    }
    fout<< "\n";
    //content
    for(i = 0; i < numCell; i++)
    {
        fout<< cellId[i] << "\t";
        for(j = 0; j < numMut; j++)
        {
            fout<< mat[i][j] << (j < numMut - 1 ? "\t" : "");
        }
        fout<< "\n";
    }

    fout.close();
}

string get_file_name(string path, bool removExtension = false)
{
    string fileName;
    size_t pos;
    // extract file name
    pos = path.find_last_of("/");
    if(pos != string::npos)
        fileName = path.substr(pos+1);
    else
        fileName = path;
    // remove extension
    if(removExtension)
    {
        pos = fileName.find_last_of(".");
        if(pos != string::npos)
            fileName = fileName.substr(0, pos);
    }
    return fileName;
}

string get_dir_path(string path)
{
    size_t pos = path.find_last_of("/");
    if(pos != string::npos)
    {
        return path.substr(0, pos);
    }
    else
    {
        return "";
    }
}

string get_exe_path()
{
  char path[10000];
  ssize_t count = readlink( "/proc/self/exe", path, 10000);
  return string(path, (count > 0) ? count : 0);
}

// double getCpuTime()
// {
// 	struct rusage t;
// 	getrusage(RUSAGE_SELF, &t);
// 	return t.ru_utime.tv_sec + t.ru_utime.tv_usec / 1000000.0 + t.ru_stime.tv_sec + t.ru_stime.tv_usec / 1000000.0;
// }

double getRealTime()
{
	struct timeval t;
	struct timezone tz;
	gettimeofday(&t, &tz);
	return t.tv_sec + t.tv_usec / 1000000.0;
}

int main(int argc, char *argv[])
{
    if(argc <= 1)
    {
        print_usage();
        exit(EXIT_FAILURE);
    }
    if(command_line_parser(argc, argv) == false)
    {
        exit(EXIT_FAILURE);
    }

	string cmd;
    
    string exeDir = get_dir_path(get_exe_path());

    // create working directory if does not exist
    // FIXME: use a more portable mkdir... int mkdir(const char *path, mode_t mode);
    cmd = "mkdir -p " + par_outDir;
    system(cmd.c_str());
    string fileName = par_outDir + "/" + get_file_name(par_inputFile, true);

    ofstream fLog((fileName + ".log").c_str());
    if(fLog.is_open() == false)
    {
        cerr<< "Could not open file: " << fileName + ".log" << endl;
        exit(EXIT_FAILURE);
    }
    fLog.precision(3);
    fLog<< fixed;

    // double cpuTime = getCpuTime();
	double realTime = getRealTime();

	get_input_data(par_inputFile);
    fLog<< "FILE_NAME: " << get_file_name(par_inputFile) << "\n";
    fLog<< "NUM_CELLS(ROWS): " << numCell << "\n";
    fLog<< "NUM_MUTATIONS(COLUMNS): " << numMut << "\n";
    fLog<< "FN_WEIGHT: " << par_fnWeight << "\n";
    fLog<< "FP_WEIGHT: " << par_fpWeight << "\n";
    fLog<< "NUM_THREADS: " << par_threads << "\n";
	// formulate as Max-SAT
	set_y_variables();
	set_x_variables();
	set_b_variables();
    if(par_maxColRemove > 0) // column elimination enabled
        set_k_variables();
	add_variable_clauses();
	add_conflict_clauses();
    if(par_maxColRemove > 0) // column elimination enabled
        add_column_clauses();
	write_maxsat_input(fileName + ".maxSAT.in");
    
    // run Max-SAT solver
    double maxsatTime = getRealTime();
    cmd = MAX_EXE + " " + fileName + ".maxSAT.in" + " > " + fileName + ".maxSAT.out";
    system(cmd.c_str());
    maxsatTime = getRealTime() - maxsatTime;

    int numFlip = -1;
    int numFlip01 = -1;
    int numFlip10 = -1;
    int numFlip20 = -1;
    int numFlip21 = -1;

    if(read_maxsat_output(fileName + ".maxSAT.out", numFlip, numFlip01, numFlip10, numFlip20, numFlip21) == false)
    {
        cerr<< "[ERROR] Max-SAT solver faild!"<< endl;
        exit(EXIT_FAILURE);
    }

    // solution is found, save it!
    write_output_matrix(fileName + ".output");
    fLog<< "MODEL_SOLVING_TIME_SECONDS: " << maxsatTime << "\n";
    fLog<< "RUNNING_TIME_SECONDS: " << getRealTime() - realTime << "\n";
    fLog<< "IS_CONFLICT_FREE: " << "YES" << "\n"; // FIXME: write the function
    fLog<< "TOTAL_FLIPS_REPORTED: " << numFlip01 + numFlip10 << "\n";
    fLog<< "0_1_FLIPS_REPORTED: " << numFlip01 << "\n";
    fLog<< "1_0_FLIPS_REPORTED: " << numFlip10 << "\n";
    fLog<< "2_0_FLIPS_REPORTED: " << numFlip20 << "\n"; // FIXME: 
    fLog<< "2_1_FLIPS_REPORTED: " << numFlip21 << "\n"; // FIXME: 
    fLog<< "MUTATIONS_REMOVED_UPPER_BOUND: " << par_maxColRemove << "\n"; // FIXME: 
    fLog<< "MUTATIONS_REMOVED_NUM: " << 0 << "\n"; // FIXME: 
    fLog<< "MUTATIONS_REMOVED_INDEX: " << "\n"; // FIXME: 

    fLog.close();

    if(remove((fileName + ".maxSAT.in").c_str()) != 0 )
        cerr<< "Could not remove file:" << fileName + ".maxSAT.in" << endl;
    if(remove((fileName + ".maxSAT.out").c_str()) != 0 )
        cerr<< "Could not remove file:" << fileName + ".maxSAT.out" << endl;

	return EXIT_SUCCESS;
}
