#include "ParameterCache.h"

using namespace std;

///
/// Constructor.
/// Also creates the directory where the parameter files get saved
/// into: plots/par
///
/// \param arg - command line options
/// \param basename - Example: gammacombo_ckm2014full_g
///
ParameterCache::ParameterCache(OptParser* arg, TString basename):
	parametersLoaded(false)
{
	assert(arg);
	this->arg = arg;
	_basename = basename;
	system("mkdir -p plots/par");
}

ParameterCache::~ParameterCache(){}

void ParameterCache::printFitResultToOutStream(ofstream &out, RooSlimFitResult *slimFitRes) {

	out << "### FCN: " << slimFitRes->minNll() << ", EDM: " << slimFitRes->edm() << endl;
	out << "### COV quality: " << slimFitRes->covQual() << ", status: " << slimFitRes->status() << ", confirmed: " << (slimFitRes->_isConfirmed?"yes":"no") << endl;
	RooArgList argList = slimFitRes->floatParsFinal();
	TIterator *iter = argList.createIterator();
	while ( RooRealVar *arg=(RooRealVar*)iter->Next() ) {
		out << Form("%-25s",arg->GetName()) << " " << Form("%12.6f",arg->getVal()) << " " << Form("%12.6f",arg->getErrorLo()) << " " << Form("%12.6f",arg->getErrorHi()) << endl;
	}
	delete iter;
}

void ParameterCache::cacheParameters(MethodAbsScan *scanner){

	if ( _basename=="" ) {
		cout << "ParameterCache::cacheParameters() : ERROR : basename is empty, didn't save file.\n"
			"Configure a basename in the constructor!" << endl;
		return;
	}
	TString fileName = getFullPath(_basename);
	int totalCachedPoints=0;
	
	// cache default solutions
	//
	cout << "ParameterCache::cacheParameters() : saving parameters to the following file " << fileName << endl;

	ofstream outfile;
	outfile.open(fileName);

	vector<RooSlimFitResult*> solutions = scanner->getSolutions();

	outfile << "##### auto-generated by ParameterCache ####### " << endl;
	TDatime d;
	outfile << "##### printed on " << d.AsString() << " ######" << endl;
	outfile << Form("%-25s","# ParameterName") << " " << Form("%12s","value") << " " << Form("%12s","errLow") << " " << Form("%12s","errHigh") << endl;

	for (unsigned int i=0; i<solutions.size(); i++){

		outfile << endl;
		outfile << "----- SOLUTION " << totalCachedPoints << " -----" << endl;

		RooSlimFitResult *slimFitRes = solutions[i];
		printFitResultToOutStream(outfile,slimFitRes);
		totalCachedPoints++;
	}
	cout << "ParameterCache::cacheParameters() : cached " << solutions.size() << " solutions" << endl;

	OptParser* arg = scanner->getArg();

	// cache also any specifically requested points
	//
	// 1D
	if (arg->savenuisances1d.size()>0){
		vector<float> &points = arg->savenuisances1d;
		for (int i=0; i<points.size(); i++){

			int iBin = scanner->getHCL()->FindBin(points[i]);
			RooSlimFitResult *r = scanner->curveResults[iBin-1];
			if (!r) {
				cout << "ParameterCache::cacheParameters() : ERROR : no fit result at this scan point!" << endl;
				return;
			}
			outfile << endl;
			outfile << "----- SOLUTION " << totalCachedPoints << " (not glob min just min at " << scanner->getScanVar1Name() << " = " << Form("%10.5f",points[i]) << " -----" << endl;
			printFitResultToOutStream(outfile,r);
			totalCachedPoints++;
		}
		cout << "ParameterCache::cacheParameters() : cached " << totalCachedPoints-solutions.size() << " further points" << endl;
	}
	// 2D
	if (arg->savenuisances2dx.size()>0){
		vector<float> &pointsx = arg->savenuisances2dx;
		vector<float> &pointsy = arg->savenuisances2dy;

		if (pointsx.size() != pointsy.size() ) {
			cout << "ParameterCache::cacheParameters() : ERROR : vectors for savenuisances2dx(y) have different size" << endl;
			return;
		}

		for (int i=0; i<pointsx.size(); i++){
			int xBin = scanner->getHCL2d()->GetXaxis()->FindBin(pointsx[i]);
			int yBin = scanner->getHCL2d()->GetXaxis()->FindBin(pointsy[i]);
			if ( xBin<1 || xBin>scanner->getNPoints2dx() || yBin<1 || yBin>scanner->getNPoints2dy() ){
				cout << "ParameterCache::cacheParameters() : ERROR : specified point is out of scan range." << endl;
				continue;
			}

			RooSlimFitResult *r = scanner->curveResults2d[xBin-1][yBin-1];
			if (!r) {
				cout << "ParameterCache::cacheParameters() : ERROR : no fit result at this scan point!" << endl;
				return;
			}
			outfile << endl;
			outfile << "----- SOLUTION " << totalCachedPoints << " (not glob min just min at " 
				<< scanner->getScanVar1Name() << " = " << pointsx[i] << " , " 
				<< scanner->getScanVar2Name() << " = " 
				<< pointsy[i] << " -----" << endl;
			printFitResultToOutStream(outfile,r);
			totalCachedPoints++;
		}
		cout << "ParameterCache::cacheParameters() : cached " << totalCachedPoints-solutions.size() << " further points" << endl;
	}
	outfile.close();
}

TString ParameterCache::getDefaultFileName()
{
	TString	fileName = getFullPath(_basename);
	fileName.ReplaceAll(".dat","_RO.dat");
	return fileName;
}

///
/// Load starting values from a file. The default is computed
/// from the basename by attaching the read-only string "_RO".
/// \param fileName - Load the parameters from a file with this name.
///										If "default" is given, the default file is loaded.
///                   Example 1: plot/par/gammacombo_ckm2014full_g.dat
///                   Example 1: default
/// \return - true, if a file was loaded
///
bool ParameterCache::loadPoints(TString fileName){

	bool successfullyLoaded = false;
	startingValues.clear();

	if ( fileName==TString("default") ) fileName = getDefaultFileName();

	ifstream infile(fileName.Data());
	if (infile) { // file exists
		cout << "ParameterCache::loadPoints() -- loading parameters from file " << fileName << endl;
		string line;
		if (infile.is_open()){
			int nSolutions=0;
			while ( getline(infile,line) ){
				if ( line.empty() ) continue; // blank line
				else if ( boost::starts_with(line,"#") ) continue; // these are comments
				else if ( boost::starts_with(line,"----") ) { // solution here
					nSolutions++;
					startingValues.push_back(map<TString,double>());
				}
				else {
					vector<string> els;
					boost::split(els,line,boost::is_any_of(" "),boost::token_compress_on);
					TString name = els[0];
					double val = boost::lexical_cast<double>(els[1]);
					startingValues[nSolutions-1].insert(make_pair(name,val));
				}
			}
		}
		parametersLoaded=true;
		successfullyLoaded=true;
		if ( arg->debug ) printPoint();
	}
	else {
		cout << "ParameterCache::loadPoints() -- no ParameterCache file found: " << fileName << endl;
	}
	return successfullyLoaded;
}

void ParameterCache::printPoint(){

	cout << "ParameterCache::printPoint() -- There are " << startingValues.size() << " solutions with values: " << endl;

	for (unsigned int i=0; i<startingValues.size(); i++){
		cout << "SOLUTION " << i << endl;
		for (map<TString,double>::iterator it = startingValues[i].begin(); it != startingValues[i].end(); it++){
			cout << Form("%-25s",it->first.Data()) << " " << Form("%12.6f",it->second) << endl;
		}
	}
}

int ParameterCache::getNPoints(){
	return startingValues.size();
}

vector<TString> ParameterCache::getFixedNames(vector<FixPar> fixPar){
	vector<TString> names;
	for (unsigned int i=0; i<fixPar.size(); i++){
		names.push_back(fixPar[i].name);
	}
	return names;
}

void ParameterCache::setPoint(MethodAbsScan *scanner, int i) {
	setPoint(scanner->getCombiner(), i);
}

void ParameterCache::setPoint(Combiner *cmb, int i) {
	if ( !parametersLoaded ){
		cout << "ParameterCache::setPoint() : ERROR : Can't set starting "
			"point as no starting values have been loaded" << endl;
		return;
	}
	if ( i>=getNPoints() ) {
		cout << "ParameterCache::setPoint() : ERROR : parameter point number " << i+1 << " not found in file." << endl;
		return;
	}
	vector<TString> fixNames = getFixedNames(cmb->getConstVars());

	RooWorkspace *w = cmb->getWorkspace();
	cout << "ParameterCache::setPoint() : Setting parameter values for point " << i+1 << endl;

	for (map<TString,double>::iterator it=startingValues[i].begin(); it!=startingValues[i].end(); it++){
		TString name = it->first;
		double val = it->second;
		if ( find(fixNames.begin(), fixNames.end(), name) != fixNames.end() ) {
			if (cmb->getArg()->debug) cout << "\tLeft " << Form("%-15s",name.Data()) << " = " << Form("%12.6f",w->var(name)->getVal()) << " constant" << endl;
			continue;
		}
		if ( w->var(name) ) {
			w->var(name)->setVal(val);
			if (cmb->getArg()->debug) cout << "\tSet  " << Form("%-15s",name.Data()) << " = " << Form("%12.6f",w->var(name)->getVal()) << endl;
		}
	}

	// if (cmb->getArg()->debug) w->allVars().Print("v");
}

///
/// Get the full path of a file from its base name. This defines
/// where the parameter files get stored.
/// \param basename - basename of the parameter file. Example: gammacombo_ckm2014full_g
/// \return full path. Example: plots/par/gammacombo_ckm2014full_g.dat
///
TString ParameterCache::getFullPath(TString basename)
{
	return Form("plots/par/%s.dat",basename.Data());
}