import numpy as np
import pandas as pd
from datetime import datetime
from itertools import *
import argparse
import os, sys, errno
from itertools import *


def read_data(file):
	df = pd.read_table(file)
	df.drop('cellID/mutID', axis=1, inplace=True)
	return df.values


def write_output(outresult, file, col_el):
	df = pd.DataFrame(outresult)
	df = df.add_prefix('mut')
	df.index = ['cell'+str(row) for row in df.index]
	df.index.name = 'cellID/mutID'
	col_el[:] = [x - 1 for x in col_el]
	df.drop(df.columns[col_el], axis=1, inplace=True)
	df.to_csv(file, sep='\t')
	return df.values


def read_vafs(file, delta, allow_vaf):
	if allow_vaf==False:
		return [], []
	df = pd.read_table(file)
	m = df.shape[0]
	vaf = []
	for i in range(m):
		vaf.append(float(df[df.columns[5]][i].split(';')[1].replace('trueVAF=','')))
	
	vafP = np.zeros(shape=(m, m)).astype(int)
	vafT = np.zeros(shape=(m, m, m)).astype(int)

	p = q = range(df.shape[0])
	loopP = list(product(p, q))
	for [p, q] in loopP:
		if p != q:
			if (vaf[p]*(1+delta)) >= vaf[q]:
				vafP[p][q] = 1
	
	p = q = t = range(m)
	loopT = list(product(p, q, t))
	for [r, a, b] in loopT:
		if p != q and p != t and q != t:
			if (vaf[p]*(1+delta)) >= (vaf[q]+vaf[t]):
				vafT[p][q][t] = 1
	
	return vafP, vafT


def compare_flips(inp, output, n, m, zeroToOne):
	totalflip = 0
	for i in range(n):
		for j in range(m):
			if zeroToOne:
				if inp[i][j] == 0 and output[i][j] == 1:
					totalflip = totalflip + 1
			else:
				if inp[i][j] == 1 and output[i][j] == 0:
					totalflip = totalflip + 1
	return totalflip


def compare_na(inp, output, n, m, twoToZero):
	totalflip = 0
	for i in range(n):
		for j in range(m):
			if twoToZero:
				if inp[i][j] == 2 and output[i][j] == 0:
					totalflip = totalflip + 1
			else:
				if inp[i][j] == 2 and output[i][j] == 1:
					totalflip = totalflip + 1
	return totalflip


def check_conflict_free(sol_matrix):
	conflict_free = True
	for p in range(sol_matrix.shape[1]):
		for q in range(p + 1, sol_matrix.shape[1]):
			oneone = False
			zeroone = False
			onezero = False
			for r in range(sol_matrix.shape[0]):
				if sol_matrix[r][p] == -1:
					return "NO"
				if sol_matrix[r][p] == 1 and sol_matrix[r][q] == 1:
					oneone = True
				if sol_matrix[r][p] == 0 and sol_matrix[r][q] == 1:
					zeroone = True
				if sol_matrix[r][p] == 1 and sol_matrix[r][q] == 0:
					onezero = True
			if oneone and zeroone and onezero:
				conflict_free = False
	if conflict_free:
		return "YES"
	else:
		return "NO"


def getX(i,j):
	return "X_" + str(i) + "_" + str(j)

def getK(i):
	return "K_" + str(i)

def getY(i,j):
	return "Y_" + str(i) + "_" + str(j)

def getB(p,q,a,b):
	return "B_" + str(p) + "_" + str(q) + "_" + str(a) + "_" + str(b)

def getA(p,q):
	return "A_" + str(p) + "_" + str(q)


def produce_input(fstr, data, numCells, numMuts, allow_col_elim, fn_weight, fp_weight, maxCol, allow_vaf, vafP, vafT):
	file = open(fstr, "w")
	#file.write("(check-sat-using smt :random-seed 1)\n")
	#file.write("(apply qflia)")
	#file.write("(check-sat-using simplify)\n")
	#file.write("(check-sat-using qflia)\n")
	#file.write("(check-sat-using (with sat :phase always-false))\n")
	#file.write("(check-sat-using (then simplify (with sat))\n")
	file.write("(check-sat-using sat)\n")
	for i in range(numCells):
		for j in range(numMuts):
			file.write("(declare-const X_" + str(i) + "_" + str(j) + " Bool)\n")
			file.write("(declare-const Y_" + str(i) + "_" + str(j) + " Bool)\n")		

	for p in range(numMuts):
		for q in range(numMuts):
			file.write("(declare-const B_" + str(p) + "_" + str(q) + "_0_1 Bool)\n")
			file.write("(declare-const B_" + str(p) + "_" + str(q) + "_1_0 Bool)\n")
			file.write("(declare-const B_" + str(p) + "_" + str(q) + "_1_1 Bool)\n")

	if allow_col_elim:
		for j in range(numMuts):
			file.write("(declare-const "+getK(j)+" Bool)\n")
	else:
		K = []

	if allow_vaf:
		for p in range(numMuts):
			for q in range(numMuts):
				file.write("(declare-const "+getA(p,q)+" Bool)\n")

	# Objective
	for i in range(numCells):
		for j in range(numMuts):
			if data[i][j] == 0:
				file.write("(assert-soft (= "+getX(i,j)+" true) :weight -"+str(fn_weight)+ ")\n")
				file.write("(assert (= "+getX(i,j)+" "+getY(i,j)+"))\n")
			elif data[i][j] == 1:
				file.write("(assert-soft (= "+getX(i,j)+" true) :weight -"+str(fp_weight)+")\n")
				file.write("(assert (not (= "+getX(i,j)+" "+getY(i,j)+")))\n")
			elif data[i][j] == 2:# NA Values
				file.write("(assert (= "+getX(i,j)+" "+getY(i,j)+"))\n")
				#file.write("(assert-soft (= "+getY(i,j)+" true) :weight -"+str((data[:,j]==0).sum())+")\n")
				#file.write("(assert-soft (= "+getY(i,j)+" false) :weight -"+str((data[:,j]==1).sum())+")\n")
			else:
				print("Error. Data entry in matrix " + fstr + " not equal to any of 0,1,2. EXITING !!!")
				sys.exit(2)

	# Constraint for not allowing removed columns go further than maxCol
	if allow_col_elim:
		for combo in combinations(range(numMuts), maxCol+1):
			temp = "(assert (not (and"
			for i in combo:
				temp = temp + " " + getK(i)
			temp = temp + ")))\n"
			file.write(temp)

	# Constraint for VAFs
	if allow_vaf:
		for p in range(numMuts):
			for q in range(numMuts):
				if p==q:
					file.write("(assert (= "+getA(p,q)+" false))\n")
				else:
					file.write("(assert (or (not "+getA(p,q)+") (not "+getA(q,p)+")))\n") #1.a
					file.write("(assert (or (not (or "+getA(p,q)+" "+getA(q,p)+")) (and (not "
											+getK(p)+") (not "+getK(q)+"))))\n") #1.b
					if vafP[p][q] == 0:
						file.write("(assert (= "+getA(p,q)+" false))\n") #1.d
				for r in range(numMuts):
					if p != q and p != r and q != r:
						if vafT[p][q][r] == 0 and q < r:
							file.write("(assert (= (and "+getA(p,q)+" "
							+getA(p,r)+" (not "+getA(q,r)+") (not "+getA(r,q)+")) false))\n") #2
							

		for t in range(numCells):
			for p in range(numMuts):
				for q in range(numMuts):
					file.write("(assert (or (not (and "+getA(p,q)+" "+getY(t,q)+")) (and "
													+getA(p,q)+" "+getY(t,p)+")))\n") #1.c



	# Constraint for checking conflict
	for i in range(numCells):
		for p in range(numMuts):
			for q in range(numMuts):
				if p <= q:
					file.write("(assert (or (not "+getY(i,p)+") (not "+getY(i,q)+") "+getB(p,q,1,1)+"))\n")
					file.write("(assert (or "+getY(i,p)+" (not "+getY(i,q)+") "+getB(p,q,0,1)+"))\n")
					file.write("(assert (or (not "+getY(i,p)+")  "+getY(i,q)+" "+getB(p,q,1,0)+"))\n")
					if allow_col_elim:
						file.write("(assert (or "+getK(p)+" "+getK(q)+" (not "
						+getB(p,q,0,1)+") (not "+getB(p,q,1,0)+") (not "+getB(p,q,1,1)+")))\n")
					else:	
						file.write("(assert (or (not "+getB(p,q,0,1)+") (not "+getB(p,q,1,0)+") (not "+getB(p,q,1,1)+")))\n")

	file.write("(check-sat)\n")
	file.write("(get-model)\n")


def exe_command(file, time_out):
	command = str(os.path.dirname(os.path.realpath(__file__)))
	command = command + '/../thirdParty/z3/build/z3 '
	if time_out > 0:
		command = command + '-t:' + str(time_out) + '000 '
	command = command + '-smt2 ' + file + " > " + file.replace('temp1', 'temp2')
	os.system(command)


def read_ouput(n, m, fstr, allow_col_elim):
	file = open(fstr, "r")
	lines = file.readlines()
	i = -1
	j = -1
	outresult = -1*np.ones(shape=(n, m)).astype(int)
	col_el = []

	if allow_col_elim:
		for index in range(len(lines)):
			line = lines[index]
			if "define-fun K" in line:
				next_line = lines[index+1]
				i = line.split(' ')[3].split('_')[1]
				i = int(i)
				if "true" in next_line:
					col_el.append(i+1)

	for index in range(len(lines)):
		line = lines[index]
		if "define-fun Y" in line:
			i = line.split(' ')[3].split('_')[1]
			j = line.split(' ')[3].split('_')[2]
			i = int(i)
			j = int(j)
			next_line = lines[index+1]
			if j+1 in col_el:
				outresult[i][j] = -1
			else:
				if "true" in next_line:
					outresult[i][j] = 1
				else:
					outresult[i][j] = 0

	return outresult, col_el


if __name__ == "__main__":
	t0 = datetime.now()
	parser = argparse.ArgumentParser(description='CSP by Z3 solver', add_help=True)
	parser.add_argument('-f', '--file', required = True,
						type = str,
						help = 'Input matrix file')
	parser.add_argument('-n', '--fnWeight', required = True,
						type = int,
						help = 'Weight for false negative')
	parser.add_argument('-p', '--fpWeight', required = True,
						type = int,
						help = 'Weight for false negative')
	parser.add_argument('-o', '--outDir', required = True,
						type = str,
						help = 'Output directory')
	parser.add_argument('-m', '--maxMut', default = 0,
						type = int,
						help = 'Max number mutations to be eliminated [0]')
	parser.add_argument('-t', '--threads', default = 1,
						type = int,
						help = 'Number of threads [1]')
	parser.add_argument('-b', '--bulk',
						type = str,
						help = 'Bulk sequencing file')
	parser.add_argument('-e', '--delta', default = 0.1,
						type = float,
						help = 'Delta in VAF [0.1]')
	args = parser.parse_args()

	inFile = args.file
	fn_weight = args.fnWeight
	fp_weight = args.fpWeight
	outDir = args.outDir
	noisy_data = read_data(inFile)
	row = noisy_data.shape[0]
	col = noisy_data.shape[1]
	tale = inFile.split('.')[-1]
	logFile = outDir+'/'+inFile.split('/')[-1].replace(tale, 'log')
	timeOut = 0

	try:
		os.makedirs(outDir)
	except OSError as exc:
		if exc.errno == errno.EEXIST and os.path.isdir(outDir):
			pass
		else:
			raise

	allow_col_elim = False
	maxCol = args.maxMut
	if maxCol == 0:
		allow_col_elim = False
		maxCol = 0
	else:
		allow_col_elim = True

	allow_vaf = False
	vafFile = ''
	vafDelta = 0
	if args.bulk is not None:
		allow_vaf = True
		vafFile = args.bulk
		vafDelta = args.delta

	vafP, vafT = read_vafs(vafFile, vafDelta, allow_vaf)
	log = open(logFile, 'w')
	produce_input(logFile.replace('log','temp1'), noisy_data, row, col, allow_col_elim, 
									fn_weight, fp_weight, maxCol, allow_vaf, vafP, vafT)
	#'''
	t1 = datetime.now()
	exe_command(logFile.replace('log','temp1'), timeOut)
	total_model = datetime.now()-t1

	output_data, col_el = read_ouput(row, col, logFile.replace('log','temp2'), allow_col_elim)
	output_mat = write_output(output_data, logFile.replace('log','output'), col_el)
	#command = "rm "+outDir+"/*.temp1"
	command = "rm " + os.path.splitext(logFile)[0]+'.temp1'
	#os.system(command)
	#command = "rm "+outDir+"/*.temp2"
	command = "rm " + os.path.splitext(logFile)[0]+'.temp2'
	#os.system(command)
	total_running = datetime.now()-t0
	
	log.write('FILE_NAME: '+inFile.split('/')[-1]+'\n')
	log.write('NUM_CELLS(ROWS): '+str(row)+'\n')
	log.write('NUM_MUTATIONS(COLUMNS): '+str(col)+'\n')
	log.write('FN_WEIGHT: '+str(fn_weight)+'\n')
	log.write('FP_WEIGHT: '+str(fp_weight)+'\n')
	log.write('NUM_THREADS: '+str(1)+'\n')
	log.write('MODEL_SOLVING_TIME_SECONDS: '+str("{0:.3f}".format(total_model.total_seconds()))+'\n')
	log.write('RUNNING_TIME_SECONDS: '+str("{0:.3f}".format(total_running.total_seconds()))+'\n')
	log.write('IS_CONFLICT_FREE: '+str(check_conflict_free(output_mat))+'\n')
	a = compare_flips(noisy_data, output_data, row, col, True)
	b = compare_flips(noisy_data, output_data, row, col, False)
	c = compare_na(noisy_data, output_data, row, col, True)
	d = compare_na(noisy_data, output_data, row, col, False)
	log.write('TOTAL_FLIPS_REPORTED: '+str(a+b)+'\n')
	log.write('0_1_FLIPS_REPORTED: '+str(a)+'\n')
	log.write('1_0_FLIPS_REPORTED: '+str(b)+'\n')
	log.write('2_0_FLIPS_REPORTED: '+str(c)+'\n')
	log.write('2_1_FLIPS_REPORTED: '+str(d)+'\n')
	log.write('MUTATIONS_REMOVED_UPPER_BOUND: '+str(maxCol)+'\n')
	log.write('MUTATIONS_REMOVED_NUM: '+str(len(col_el))+'\n')
	temp = 'MUTATIONS_REMOVED_INDEX: '+ "," . join([str(i) for i in sorted(col_el)])
	log.write(temp+'\n')
	#'''