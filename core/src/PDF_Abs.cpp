/**
 * Gamma Combination
 * Author: Till Moritz Karbach, moritz.karbach@cern.ch
 * Date: August 2012
 *
 **/

#include "PDF_Abs.h"

PDF_Abs::PDF_Abs(int nObs)
: covMatrix(nObs),
  corMatrix(nObs),
  corStatMatrix(nObs),
  corSystMatrix(nObs)
{
  this->nObs = nObs;
	parameters = 0;
  theory = 0;
	observables = 0;
  pdf = 0;
  toyObservables = 0;
  nToyObs = 1000;
  iToyObs = 0;
  for ( int i=0; i<nObs; i++ ){
    StatErr.push_back(0.0);
    SystErr.push_back(0.0);
  }
  title = "(no title)";
  corSource = "n/a";
  obsValSource = "n/a";
  obsErrSource = "n/a";
  uniqueID = "UID0";
  counter++;
  uniqueGlobalID = counter;
	m_isCrossCorPdf = false;
	gcId = -1;
}

unsigned long long PDF_Abs::counter = 0;

///
/// Clean off all objects in the trash bin.
/// See also addToTrash().
///
PDF_Abs::~PDF_Abs()
{
  // clean objects in the 'parameters' container
  for(int i=0; i<parameters->getSize(); i++){
    delete parameters->at(i);
  }
  delete parameters;
  
  // clean objects in the 'theory' container
  for(int i=0; i<theory->getSize(); i++){
    delete theory->at(i);
  }
  delete theory;

  // clean objects in the 'observables' container
  for(int i=0; i<observables->getSize(); i++){
    delete observables->at(i);
  }
  delete observables;
  
  // clean pregenerated toys
  if ( toyObservables!=0 ) delete toyObservables;
  
  // clean pdf
  if ( pdf!=0 ) delete pdf;
  
  // empty trash
  map<string, TObject*>::iterator iter;
  for(iter = trash.begin(); iter != trash.end(); ++iter)
  {
    if ( iter->second )
    {
      delete iter->second;
      iter->second = 0;
    }
  }
}

void PDF_Abs::initParameters(){assert(0);};
void PDF_Abs::initRelations(){assert(0);};
void PDF_Abs::initObservables(){assert(0);};
void PDF_Abs::buildPdf(){assert(0);};
void PDF_Abs::setObservables(config c){assert(0);};
void PDF_Abs::setUncertainties(config c){assert(0);};
void PDF_Abs::setCorrelations(config c){assert(0);};

///
/// Shortcut for buildCov() and buildPdf().
///
void PDF_Abs::build()
{
	buildCov();
	buildPdf();
}

///
/// Set all observables to 'truth' values computed from the
/// current parameters.
///
void PDF_Abs::setObservablesTruth()
{
  obsValSource = "truth";
  for ( int i=0; i<nObs; i++ )
  {
    RooRealVar* pObs = (RooRealVar*)((RooArgList*)observables)->at(i);
    pObs->setVal(((RooRealVar*)((RooArgList*)theory)->at(i))->getVal());
  }
}

///
/// Set all observables to 'toy' values drawn from the
/// PDF using the current parameter values. A certain number
/// of toys is pregenerated to speed up when doing mulitple toy fits.
///
void PDF_Abs::setObservablesToy()
{
  obsValSource = "toy";
  if( !pdf ){ cout<< "PDF_Abs::setObservables(): ERROR: pdf not initialized."<<endl; exit(1); }
  if ( toyObservables==0 || iToyObs==nToyObs )
  {
    RooRandom::randomGenerator()->SetSeed(0);
    if ( iToyObs==nToyObs ) delete toyObservables;
    toyObservables = pdf->generate(*(RooArgSet*)observables, nToyObs);
    iToyObs=0;
  }
  for ( int i=0; i<nObs; i++ )
  {
    RooRealVar* pObs = (RooRealVar*)((RooArgList*)observables)->at(i);
    pObs->setVal(((RooRealVar*)toyObservables->get(iToyObs)->find(pObs->GetName()))->getVal());
  }
  iToyObs+=1;
}

///
/// Set all correlations to zero.
///
void PDF_Abs::resetCorrelations()
{
  for ( int i=0; i<nObs; i++ )
  for ( int j=0; j<nObs; j++ )
	{
    float c = 0.0;
    if ( i==j ) c = 1.0;
    corStatMatrix[i][j] = c;
		corSystMatrix[i][j] = c;
	}
}

///
/// Add an object to the trash bin which gets emptied
/// when this PDF object gets deleted. If an object of
/// the same name is already in the trash, we'll delete
/// that and replace by the new one. This way we can call
/// e.g. buildPdf() more than once.
///
void PDF_Abs::addToTrash(TObject* o)
{
  map<string, TObject*>::iterator iter;
  for (iter = trash.begin(); iter != trash.end(); ++iter){
    if ( o->GetName() == iter->first ){
      delete iter->second;
      iter->second = o;
      return;
    }
  }
  trash.insert(pair<string,TObject*>(o->GetName(),o));
}

///
/// Return the base name, which is the name without any
/// unique ID.
///
TString PDF_Abs::getBaseName()
{
  TString baseName = name;
  baseName.ReplaceAll(uniqueID,"");
  return baseName;
}

///
/// Uniquify all relevant names. This way we can have mulitple
/// instances of the same PDF in the same combination.
///
void PDF_Abs::uniquify(int uID)
{
	if ( uniqueID==TString("") ) {
    cout << "PDF_Abs::uniquify() : ERROR : uniqueID is the empty string!" << endl;
    exit(1);
  }
  
  name = uniquifyThisString(name, uID);
  // change name of pdf
  pdf->SetName(uniquifyThisString(pdf->GetName(), uID));

	// change names of observables and parameters, but not for
	// cross correlation PDFs, because they need the same names as
	// the main PDFs
	if ( !m_isCrossCorPdf ){
	  // change names of observables
	  for ( int i=0; i<observables->getSize(); i++ ){
	    observables->at(i)->SetName(uniquifyThisString(observables->at(i)->GetName(), uID));
		}
	  // change names of theory parameters
	  for ( int i=0; i<theory->getSize(); i++ ){
	    theory->at(i)->SetName(uniquifyThisString(theory->at(i)->GetName(), uID));
		}
  }
  uniqueID = uniquifyThisString("", uID);
}

///
/// Helper function for uniquify(). Compute a unique string
/// by attaching "UID3" or similar to it.
///
TString PDF_Abs::uniquifyThisString(TString s, int uID)
{
  TString newUniqueID = Form("UID%i",uID);
  if ( s.Contains(uniqueID) ) s.ReplaceAll(uniqueID,newUniqueID);
  else s = s + newUniqueID;
  return s;
}

///
/// Set all parameters to values found in
/// a provided fit result.
///
void PDF_Abs::loadExtParameters(RooFitResult *r)
{
  RooArgSet *tmp = new RooArgSet();
  tmp->add(r->floatParsFinal());
  tmp->add(r->constPars());
  setParameters(parameters, tmp);
  delete tmp;
}

///
/// Build both the covariance and the correlation matrix
/// (members covMatrix and corMatrix)
/// from the stat and syst correlation matrics and the
/// respective errors.
///
void PDF_Abs::buildCov()
{  
  // add diagonals, symmetrize
  buildCorMatrix(corStatMatrix);
  buildCorMatrix(corSystMatrix);
	
	// make total cov matrix
  TMatrixDSym *covStat = buildCovMatrix(corStatMatrix, StatErr);
  TMatrixDSym *covSyst = buildCovMatrix(corSystMatrix, SystErr);
  covMatrix = *covStat + *covSyst;
  
  // check if total cov matrix is invertible
	if ( covMatrix.Determinant()==0 ) {
    cout << "PDF_Abs::buildCov() : ERROR : Total covariance matrix is not invertable (det(COV)=0)." << endl;
    cout << "PDF_Abs::buildCov() : ERROR : Check inputs! Ordering correct? Nobs correct?" << endl;
    cout << "PDF_Abs::buildCov() : PDF: " << name << endl;
    cout << "PDF_Abs::buildCov() : stat cov: " << endl;
    covStat->Print("v");
    cout << "PDF_Abs::buildCov() : syst cov: " << endl;
    covSyst->Print("v");
    cout << "PDF_Abs::buildCov() : full cov: " << endl;
    covMatrix.Print("v");
    //exit(1);
    throw TString("need help");
  }
  
  // make total cor matrix
  for ( int i=0; i<covMatrix.GetNcols(); i++ )
  for ( int j=0; j<covMatrix.GetNcols(); j++ )
	{
    corMatrix[i][j] = covMatrix[i][j]/sqrt(covMatrix[i][i])/sqrt(covMatrix[j][j]);
	}
	
	// check if total cor matrix is positive definite
	if ( ! isPosDef(&corMatrix) ) {
    cout << "PDF_Abs::buildCov() : ERROR : Total correlation matrix is not positive definite." << endl;
    cout << "PDF_Abs::buildCov() : ERROR : Check inputs! Ordering correct?" << endl;
		cout << "PDF_Abs::buildCov() :         Sometimes this happens when for very large correlations" << endl;
		cout << "PDF_Abs::buildCov() :         the given precision is not enough (e.g. rho=0.98 rather than 0.978)." << endl;
    cout << "PDF_Abs::buildCov() : PDF: " << name << endl;
    cout << "PDF_Abs::buildCov() : stat cor: " << endl;
    corStatMatrix.Print("v");
    cout << "PDF_Abs::buildCov() : syst cor: " << endl;
    corSystMatrix.Print("v");
    //exit(1);
    throw TString("need help");
  }
	
  delete covStat;
  delete covSyst;
  
  // this is needed for the pull computation and the PDF_Abs::print() function:
  storeErrorsInObs();
}

///
/// Helper function for print(): it prints correlation matrices,
/// stat, syst, stat+syst
///
void PDF_Abs::printCorMatrix(TString title, TString source, const TMatrixDSym& cor) const
{
	cout << "    correlation " << title << ":" << endl;
  cout << "      cor. source: " << source << endl;
  printf("%30s", " ");
  for ( int i=0; i<nObs; i++ ) printf("%5i ", i);
  cout << endl;
  for ( int i=0; i<nObs; i++ ){
    TString obsName = ((RooRealVar*)((RooArgList*)observables)->at(i))->GetName();
    obsName.ReplaceAll(uniqueID,"");
    printf("      %-20s %2i ", obsName.Data(), i);
    for ( int j=0; j<nObs; j++ ){
      if (fabs(cor[i][j])<0.005) printf("%5s ", "-");
      else printf("%5.2f ", cor[i][j]);
    }
    cout << endl;
  }
  cout << endl;
}

///
/// Print this PDF in a verbose way:
/// - observables
/// - correlations
/// - parameters
///
void PDF_Abs::print() const
{
  TString cleanName = name;
  cout << "PDF: " << cleanName.ReplaceAll(uniqueID,"") << " (" << uniqueID << ")" << endl << endl;
  
  if ( observables ){
    cout << "    observables:" << endl;
    cout << "      nObs = " << nObs << endl;
    cout << "      values from: " << obsValSource << endl;
    cout << "      errors from: " << obsErrSource << endl;
    for ( int iObs=0; iObs<nObs; iObs++ ){
      RooRealVar* v = (RooRealVar*)observables->at(iObs);
      TString obsName = v->GetName();
      obsName.ReplaceAll(uniqueID,"");
      printf("      %-20s = %8.5f +/- %7.5f +/- %7.5f\n", obsName.Data(), v->getVal(), StatErr[iObs], SystErr[iObs]);
    }
  }
  else cout << "PDF_Abs::print() : observables not initialized. Call initObservables() first." << endl;
  cout << endl;
  
  if ( nObs>1 ){
		printCorMatrix("(stat+syst)", corSource, corMatrix);
		printCorMatrix("(stat)", corSource, corStatMatrix);
		printCorMatrix("(syst)", corSource, corSystMatrix);
  }
  
  if ( parameters ){
    cout << "    parameters:" << endl;
    cout << "      nPar = " << parameters->getSize() << endl;
    cout << "      ";
    bool first=true;
    TIterator* it = parameters->createIterator();
    while ( RooAbsReal* v = (RooAbsReal*)it->Next() ){
      cout << (first?"":", ") << v->GetName();
      first=false;
    }
    delete it;
    cout << endl;
  }
  else cout << "PDF_Abs::print() : parameters not initialized. Call initParameters() first." << endl;
  cout << endl;
  
  if ( theory ){
    cout << "    relations:" << endl;
    TIterator* it = theory->createIterator();
    while ( RooAbsReal* v = (RooAbsReal*)it->Next() ){
      // it's not easy to extract the formula from a RooFormulaVar.
      TString thName = v->GetName();
      thName.ReplaceAll(uniqueID,"");
      printf("      %-20s = ", thName.Data());
      ostringstream stream;
      v->printMetaArgs(stream);
      TString formula = stream.str();
      formula.ReplaceAll("formula=", "");
      formula.ReplaceAll("\"", "");
      if ( formula=="" ) formula = v->ClassName(); // compiled custom Roo*Var classes don't have a formula
      cout << formula << endl;
    }
    delete it;
  }
  else cout << "PDF_Abs::print() : theory not initialized. Call initRelations() first." << endl;
  cout << endl;
}


void PDF_Abs::printParameters()
{
  if ( parameters )
  {
    cout << "      parameters:  ";
    bool first=true;
    TIterator* it = parameters->createIterator();
    while ( RooAbsReal* v = (RooAbsReal*)it->Next() )
    {
      TString vName = v->GetName();
      cout << (first?"":", ") << vName;
      first=false;
    }
    cout << "  (nPar=" << parameters->getSize() << ")" << endl;
  }
  else cout << "PDF_Abs::print() : parameters not initialized. Call initParameters() first." << endl;
}


void PDF_Abs::printObservables()
{
  if ( observables ){
    cout << "      observables: ";
    bool first=true;
    TIterator* it = observables->createIterator();
    while ( RooAbsReal* v = (RooAbsReal*)it->Next() ){
      TString vName = v->GetName();
      vName.ReplaceAll("_obs","");
      vName.ReplaceAll(uniqueID,"");
      cout << (first?"":", ") << vName;
      first=false;
    }
    cout << "  (nObs=" << observables->getSize() << ")" << endl;
  }
  else cout << "PDF_Abs::print() : observables not initialized. Call initObservables() first." << endl;
}

///
/// Store the errors as RooFit error into the observables
/// to have them easily available for the pull computation.
///
void PDF_Abs::storeErrorsInObs()
{
  if ( covMatrix==0 )
  {
    cout << "PDF_Abs::storeErrorsInObs() : ERROR : covMatrix not initialized." << endl;
    return;
  }
  
  for ( int i=0; i<nObs; i++ )
  {
    RooRealVar* pObs = (RooRealVar*)((RooArgList*)observables)->at(i);
    pObs->setError(sqrt(covMatrix[i][i]));
  }
}

///
/// Set an external systematic correlation matrix.
/// After modifying, call buildCov() and buildPdf();
///
void PDF_Abs::setSystCorrelation(TMatrixDSym &corSystMatrix)
{
  assert(corSystMatrix.GetNcols()==nObs);
  this->corSystMatrix = corSystMatrix;
  corSource = corSource + " (syst. cor. set manually)";
}

void PDF_Abs::setObservable(TString name, float value)
{
  RooRealVar* obs = (RooRealVar*)observables->find(name);
  if ( obs==0 ) { cout << "PDF_Abs::setObservable() : ERROR : observable "+name+" not found!" << endl; exit(1); }
  obs->setVal(value);
}

///
/// Perform a couple of consistency checks to make it easier
/// to find bugs.
///
bool PDF_Abs::checkConsistency()
{
	if ( m_isCrossCorPdf ) return true;
  bool allOk = true;
  
  // check if all observables end with '_obs'
  TIterator* it = observables->createIterator();
  while ( RooRealVar* p = (RooRealVar*)it->Next() ){
    TString pObsName = p->GetName();
    pObsName.ReplaceAll(uniqueID,"");
    if ( !pObsName.EndsWith("_obs") ){
      cout << "PDF_Abs::checkConsistency() : " << name << " : observable " << p->GetName() << " doesn't end with '_obs'" << endl;
      allOk = false;
    }
  }

  // check if all predicted observables end with '_th'
  delete it; it = theory->createIterator();
  while ( RooRealVar* p = (RooRealVar*)it->Next() ){
    TString pThName = p->GetName();
    pThName.ReplaceAll(uniqueID,"");
    if ( !pThName.EndsWith("_th") ){
      cout << "PDF_Abs::checkConsistency() : " << name << " : theory " << p->GetName() << " doesn't end with '_th'" << endl;
      allOk = false;
    }
  }
  
  // check if the 'observables' and 'theory' lists are correctly ordered
  for ( int i=0; i<nObs; i++ ){
    RooAbsArg* pTh = theory->at(i);
    TString base = pTh->GetName();
    base.ReplaceAll("_th","");
    base.ReplaceAll(uniqueID,"");
    TString pObsName = observables->at(i)->GetName();
    pObsName.ReplaceAll(uniqueID,"");
    if ( pObsName != base+"_obs"){
      cout << "PDF_Abs::checkConsistency() : " << name << " : " << pTh->GetName() << " doesn't match its observable.\n"
              "                              Expected '" << base+"_obs" << "'. Found '" << pObsName << "'.\n"
              "                              Check ordering of the 'theory' and 'observables' lists!" << endl;
      allOk = false;
    }
  }
  
  return allOk;
}

///
/// Test PDF implementation.
/// Performs a fit to the minimum.
///
bool PDF_Abs::test()
{
  bool quiet = false;
  if(quiet) RooMsgService::instance().setGlobalKillBelow(ERROR);
  fixParameters(observables);
  floatParameters(parameters);
  setLimit(parameters, "free");
  RooFormulaVar ll("ll", "ll", "-2*log(@0)", RooArgSet(*pdf));
  RooMinuit m(ll);
  if(quiet) m.setPrintLevel(-2);
  m.setNoWarn();
  m.setLogFile("/dev/zero");  
  m.setErrorLevel(1.0);
  m.setStrategy(2);
  // m.setProfile(1);
  m.migrad();
  RooFitResult *f = m.save();
  bool status = !(f->edm()<1 && f->status()==0);
  if(!quiet) f->Print("v");
  delete f;
  if(quiet) RooMsgService::instance().setGlobalKillBelow(INFO);
  if(!quiet) cout << "pdf->getVal() = " << pdf->getVal() << endl;
  return status;
}