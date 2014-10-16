/*
 * Gamma Combination
 * Author: Till Moritz Karbach, moritz.karbach@cern.ch
 * Date: August 2012
 *
 */

#include "MethodPluginScan.h"

///
/// Initialize from a previous Prob scan, setting the profile
/// likelihood. This should be the default.
///
MethodPluginScan::MethodPluginScan(MethodProbScan* s)
: MethodAbsScan(s->getCombiner())
{
  methodName = "Plugin";
  title = s->getTitle();
  scanVar1 = s->getScanVar1Name();
  scanVar2 = s->getScanVar2Name();
  profileLH = s;
  parevolPLH = profileLH;
  setSolutions(s->getSolutions());
  setChi2minGlobal(s->getChi2minGlobal());
  obsDataset = new RooDataSet("obsDataset", "obsDataset", *w->set(obsName));
	obsDataset->add(*w->set(obsName));
  nToys = arg->ntoys;
  nPoints1d  = arg->npointstoy;
  nPoints2dx = arg->npointstoy;
  nPoints2dy = arg->npointstoy;
}

///
/// 'Default constructor', mainly to ensure compatibility with MethodGenericPluginScan
/// this way one is not forced to use an explicit constructor
///
MethodPluginScan::MethodPluginScan(){
  methodName = "Plugin";
};

///
/// Initialize from a Combiner object. This is more difficult,
/// as now we have to set the profile likelihood explicitly,
/// and probably some other things...
/// But to just compute a p-value at a certain point (getPvalue1d())
/// this is fine.
///
MethodPluginScan::MethodPluginScan(Combiner* comb)
: MethodAbsScan(comb)
{
  methodName = "Plugin";
  title = comb->getTitle();
  profileLH = 0;
  parevolPLH = 0;
  obsDataset = new RooDataSet("obsDataset", "obsDataset", *comb->getWorkspace()->set(obsName));
	obsDataset->add(*comb->getWorkspace()->set(obsName));
  nToys = arg->ntoys;
  nPoints1d  = arg->npointstoy;
  nPoints2dx = arg->npointstoy;
  nPoints2dy = arg->npointstoy;
}

///
/// Set the parameter evolution over the profile likelihood
/// that was previously computed by a MethodProbScan scanner. Usually, the PLH is used that
/// is provided to the constructor. Use this method to use a different evolution for toy
/// generation (Hybrid Plugin).
///
void MethodPluginScan::setParevolPLH(MethodProbScan* s)
{
  assert(s);
  parevolPLH = s;
}

///
/// Helper function for scan1d(). Gets point in parameter space (in form
/// of a RooFitResult) at which the plugin toy should be generated.
/// The point is taken from the externally provided scanner parevolPLH,
/// which is usually just the profile likelihood, but can also be set
/// to the profile likelihood of a different combination.
/// A couple of sanity checks are performed.
///
/// \param scanpoint - value of the scan parameter for which the point
///                    should be found
///
RooSlimFitResult* MethodPluginScan::getParevolPoint(float scanpoint)
{
	// get point in nuisance parameter space where the toys get generated at
  int iCurveRes = parevolPLH->getHCL()->FindBin(scanpoint)-1;
  if ( !parevolPLH->curveResults[iCurveRes] ){
    cout << "MethodPluginScan::getParevolPoint() : ERROR : curve result not found, id=" << iCurveRes;
    cout << ", scanpoint=" << scanpoint << endl;
    exit(1);
  }

  // check that the scan variable is indeed present
  RooArgList list = parevolPLH->curveResults[iCurveRes]->floatParsFinal();
  list.add(parevolPLH->curveResults[iCurveRes]->constPars());
  RooRealVar* var = (RooRealVar*)list.find(scanVar1);
  if ( !var ){
    cout << "MethodPluginScan::getParevolPoint() : ERROR : "
    "scan variable not found in parameter evolution, var=" << scanVar1 << endl;
    cout << "MethodPluginScan::getParevolPoint() : Printout follows:" << endl;
    parevolPLH->curveResults[iCurveRes]->Print();
    exit(1);
  }

  // check if the scan variable here differs from that of
  // the external curve
  if ( fabs((scanpoint-var->getVal())/scanpoint) > 0.01 ){
    cout << "MethodPluginScan::getParevolPoint() : WARNING : "
    "scanpoint and parameter evolution point differ by more than 1%:" << endl;
    cout << scanpoint << " " << var->getVal() << endl;
  }

	return parevolPLH->curveResults[iCurveRes];
}

///
/// Generate toys.
///
/// \param nToys - generate this many toys
///
RooDataSet* MethodPluginScan::generateToys(int nToys)
{
	RooRandom::randomGenerator()->SetSeed(0);
	RooMsgService::instance().setStreamStatus(0,kFALSE);
  RooMsgService::instance().setStreamStatus(1,kFALSE);
	RooDataSet* dataset = w->pdf(pdfName)->generate(*w->set(obsName), nToys, AutoBinned(false));
	RooMsgService::instance().setStreamStatus(0,kTRUE);
  RooMsgService::instance().setStreamStatus(1,kTRUE);

	// Test toy generation - print out the first 10 toys to stdout.
	// Triggered by --qh 5
	if ( arg->isQuickhack(5) ){
		if ( w->var("kD_k3pi") ) cout << "kD_k3pi=" << w->var("kD_k3pi")->getVal() << endl;
		if ( w->var("dD_k3pi") ) cout << "dD_k3pi=" << w->var("dD_k3pi")->getVal() << endl;
		for ( int j = 0; j<10 && j<nToys; j++ ){
			const RooArgSet* toyData = dataset->get(j);
			toyData->Print("v");
		}
	}

	// The following code is an embarrasment. Something goes wrong
	// in RooFit's toy generation when a Th2F PDF is used. We try to
	// work around it.
	// Sometimes there is an error from TFoam (the TH2F clearly isn't zero):
	// Error in <TFoam::MakeActiveList>: Integrand function is zero
	// Then it proceeds "generating" observables that are, in every toy, set to their boundaries.
	// This happens only for kD_k3pi_obs.
	// Workaround: If it happens, flucutate the parameters of the histogram
	// ever so slightly and regenerate.

	// read the generated values for one variable from the
	// first two toys
	bool hasK3pi = false;
	float generatedValues[2];
	for ( int i=0; i<2; i++ ){
		const RooArgSet* toyData = dataset->get(i);
		TIterator* it = toyData->createIterator();
	  while(RooRealVar* var = (RooRealVar*)it->Next()){
			if ( TString(var->GetName()).Contains("kD_k3pi_obs") ){
				hasK3pi = true;
				generatedValues[i] = var->getVal();
				continue;
			}
		}
		delete it;
	}

	// check if they are the same, if so, fluctuate and regenerate
  if ( hasK3pi && generatedValues[0]==generatedValues[1] ){
		delete dataset;
		cout << "kD_k3pi_obs GENERATION ERROR AT kD_k3pi=" << w->var("kD_k3pi")->getVal()
			<< " dD_k3pi=" << w->var("dD_k3pi")->getVal() << endl;
		TRandom3 r;
		w->var("kD_k3pi")->setVal(r.Gaus(w->var("kD_k3pi")->getVal(),0.05));
		w->var("dD_k3pi")->setVal(r.Gaus(w->var("dD_k3pi")->getVal(),0.05));
		cout << "kD_k3pi_obs SECOND GENERATION AT kD_k3pi=" << w->var("kD_k3pi")->getVal()
			<< " dD_k3pi=" << w->var("dD_k3pi")->getVal() << endl;
		RooMsgService::instance().setStreamStatus(0,kFALSE);
	  RooMsgService::instance().setStreamStatus(1,kFALSE);
		dataset = w->pdf(pdfName)->generate(*w->set(obsName), nToys, AutoBinned(false));
		RooMsgService::instance().setStreamStatus(0,kTRUE);
	  RooMsgService::instance().setStreamStatus(1,kTRUE);		for ( int i=0; i<2; i++ ){
			const RooArgSet* toyData = dataset->get(i);
			TIterator* it = toyData->createIterator();
			while(RooRealVar* var = (RooRealVar*)it->Next()){
				if ( TString(var->GetName()).Contains("kD_k3pi_obs") ){
					generatedValues[i] = var->getVal();
					continue;
				}
			}
			delete it;
		}
		cout << "kD_k3pi_obs NEW VALUES : toy 0: " << generatedValues[0] << " toy 1: " << generatedValues[1] << endl;
  }

	return dataset;
}

///
/// Compute the p-value at a certain point in parameter space using
/// the plugin method. The precision of the p-value will depend on
/// the number of toys that were generated, more than 100 should
/// be a good start (ntoys command line option).
///
/// \param plhScan  Defines the point in parameter space (=nuisances) at
///                 which the p-value gets computed. That is, it all toys
///                 are being generated at this point. It also defines the
///                 value of the scan parameter and provides the "scan"
///                 chi2 value of "the data".
/// \param plhFree  Defines the best fit point of "the data" in parameter
///                 space. Here we only need its chi2 value to compute
///                 the Delta chi2 for "the data".
/// \param t        Stores all toys and fit results that are being generated.
///                 If a ToyTree is provided, entries will be added to the
///                 existing tree, and all new entries will have the
///                 "id" branch set to the id parameter. If no tree is
///                 provided, a new one will be created. This will be saved
///                 to disk and deleted from memory afterwards.
/// \param id       An arbitrary number identifying this particular run of
///                 getPvalue1d(). E.g. in the coverage tests, we need to
///                 run many times for different toy data sets that replace
///                 the nominal "data". The ToyTree also contains a 'run' branch
///                 that holds the run number (=batch job).
/// \param f        A fitter object. If not given, a new one will be created.
///                 It may be useful to use an external fitter so that the
///                 fitter object can compute some fit statistics for an entire
///                 1-CL scan.
/// \return         the p-value.
///
void MethodPluginScan::computePvalue1d(RooSlimFitResult* plhScan, double chi2minGlobal, ToyTree* t, int id,
	Fitter* f, ProgressBar *pb)
{
  // Check inputs.
  assert(plhScan);
	assert(t);
	assert(f);
	assert(pb);
  if ( !plhScan->hasParameter(scanVar1) ){
    cout << "MethodPluginScan::getPvalue1d() : ERROR : scan variable not found in plhScan. Exit." << endl;
    assert(0);
  }

	// Save parameter values that were active at function
	// call. We'll reset them at the end to be transparent
	// to the outside.
	FitResultCache frCache(arg);
	frCache.storeParsAtFunctionCall(w->set(parsName));
	frCache.initRoundRobinDB(w->set(parsName));

  // Set nuisances. This is the point in parameter space where
  // the toys need to be generated.
  setParameters(w, parsName, plhScan, true);

	// save nuisances for start parameters
	frCache.storeParsAtGlobalMin(w->set(parsName));

  // set and fix scan point
  RooRealVar *par = w->var(scanVar1);
  par->setConstant(true);
  float scanpoint = par->getVal();

  // get the chi2 of the data
  t->scanpoint = scanpoint;
  t->id = id;
  t->storeParsPll();
  t->storeTheory();
  t->chi2min = plhScan->minNll();
  t->chi2minGlobal = chi2minGlobal;

	// Importance sampling
	int nActualToys = nToys;
	if ( arg->importance ){
	  float plhPvalue = TMath::Prob(t->chi2min - t->chi2minGlobal,1);
	  nActualToys = nToys*importance(plhPvalue);
	  pb->skipSteps(nToys-nActualToys);
	}

  // Draw all toy datasets in advance. This is much faster.
	RooDataSet *toyDataSet = generateToys(nActualToys);

	for ( int j = 0; j<nActualToys; j++ )
	{
		pb->progress();

		//
		// 1. Generate toys
		//    (or select the right one)
		//
    const RooArgSet* toyData = toyDataSet->get(j);
    setParameters(w, obsName, toyData);
    t->storeObservables();

		//
		// 2. scan fit
		//
    par->setVal(scanpoint);
    par->setConstant(true);
    f->setStartparsFirstFit(frCache.getRoundRobinNminus(0));
    f->setStartparsSecondFit(frCache.getParsAtGlobalMin());
    f->fit();
		if ( f->getStatus()==1 ){
      f->setStartparsFirstFit(frCache.getRoundRobinNminus(1));
      f->setStartparsSecondFit(frCache.getRoundRobinNminus(2));
      f->fit();
		}
    t->chi2minToy = f->getChi2();
    t->statusScan = f->getStatus();
    t->storeParsScan();

		//
		// 3. free fit
		//
    par->setConstant(false);
    f->fit();
		if ( f->getStatus()==1 ){
      f->fit();
		}
    t->chi2minGlobalToy = f->getChi2();
    t->statusFree = f->getStatus();
    t->scanbest = ((RooRealVar*)w->set(parsName)->find(scanVar1))->getVal();
    t->storeParsFree();

    //
    // 4. store
    //
		if ( t->statusFree==0 ) frCache.storeParsRoundRobin(w->set(parsName));
    t->fill();
	}

	// clean up
	setParameters(w, parsName, frCache.getParsAtFunctionCall());
	setParameters(w, obsName, obsDataset->get(0));
  delete toyDataSet;
}

double MethodPluginScan::getPvalue1d(RooSlimFitResult* plhScan, double chi2minGlobal, ToyTree* t, int id)
{
	// Create a ToyTree to store the results of all toys
	// (or use the supplied one so we can have a ToyTree
	// that holds a full scan).
  ToyTree *myTree = 0;
	if ( !t ){
	  myTree = new ToyTree(combiner);
    myTree->init();
  }
  else{
    myTree = t;
  }

	// Create a fitter
  Fitter *myFit = new Fitter(arg, w, combiner->getPdfName());

	// Create a progress bar
	ProgressBar *myPb = new ProgressBar(arg, nToys);

	// do the work
	cout << "MethodPluginScan::getPvalue1d() : computing p-value ..." << endl;
	computePvalue1d(plhScan, chi2minGlobal, myTree, id, myFit, myPb);

	// compute p-value
  if ( arg->controlplot ) myTree->ctrlPlotSummary();
  TH1F *h = analyseToys(myTree, id);
	float scanpoint = plhScan->getParVal(scanVar1);
  double pvalue = h->GetBinContent(h->FindBin(scanpoint));
  delete h;

	if ( !t ){
    myTree->writeToFile(Form("root/getPvalue1d_"+name+"_"+scanVar1+"_run%i.root", arg->nrun));
    delete myTree;
  }
  delete myFit;
	delete myPb;
	return pvalue;
}

///
/// Perform the 1d Plugin scan.
/// Saves chi2 values in a root tree, together with the full fit result for each toy.
/// If option --lightfiles is given, the tree will only contain the essentials (min Chi2).
/// If a combined PDF for the toy generation is given by setParevolPLH(), this
/// will be used to generate the toys.
///
/// \param nRun Part of the root tree file name to facilitate parallel production.
///
int MethodPluginScan::scan1d(int nRun)
{
  Fitter *myFit = new Fitter(arg, w, combiner->getPdfName());
	RooRandom::randomGenerator()->SetSeed(0);

	// Set limit to all parameters.
  combiner->loadParameterLimits();

	// Define scan parameter and scan range.
  RooRealVar *par = w->var(scanVar1);
  float min = hCL->GetXaxis()->GetXmin();
  float max = hCL->GetXaxis()->GetXmax();

	if ( arg->verbose ){
    cout << "Plugin configuration:" << endl;
    cout << "  combination : " << title << endl;
    cout << "  scan variable : " << scanVar1 << endl;
    cout << "  scan range : " << min << " ... " << max << endl;
    cout << "  scan steps : " << nPoints1d << endl;
    cout << "  par. evolution : " << (parevolPLH!=profileLH?parevolPLH->getTitle():"same as combination") << endl;
    cout << "  nToys : " << nToys << endl;
    cout << endl;
  }

	// Set up toy root tree
  ToyTree t(combiner);
  t.init();
  t.nrun = nRun;

	// Save parameter values that were active at function
	// call. We'll reset them at the end to be transparent
	// to the outside.
	FitResultCache frCache(arg);
	frCache.storeParsAtFunctionCall(w->set(parsName));

	// for the progress bar: if more than 100 steps, show 50 status messages.
	int allSteps = nPoints1d*nToys;
	ProgressBar *pb = new ProgressBar(arg, allSteps);

  // start scan
  cout << "MethodPluginScan::scan1d() : starting ..." << endl;
	for ( int i=0; i<nPoints1d; i++ )
	{
    float scanpoint = min + (max-min)*(double)i/(double)nPoints1d + hCL->GetBinWidth(1)/2.;
    t.scanpoint = scanpoint;

    // don't scan in unphysical region
    if ( scanpoint < par->getMin() || scanpoint > par->getMax() ) continue;

    // Get nuisances. This is the point in parameter space where
    // the toys need to be generated.
    RooSlimFitResult* plhScan = getParevolPoint(scanpoint);

		// do the work
		computePvalue1d(plhScan, profileLH->getChi2minGlobal(), &t, i, myFit, pb);

		// reset
		setParameters(w, parsName, frCache.getParsAtFunctionCall());
		setParameters(w, obsName, obsDataset->get(0));
	}

  myFit->print();
  TString dirname = "root/scan1dPlugin_"+name+"_"+scanVar1;
  system("mkdir -p "+dirname);
	t.writeToFile(Form(dirname+"/scan1dPlugin_"+name+"_"+scanVar1+"_run%i.root", nRun));
  delete myFit;
	delete pb;
  if (!(arg->isAction("pluginbatch"))) readScan1dTrees(nRun, nRun);
  return 0;
}

///
/// Perform the 2d Plugin scan.
/// Saves chi2 values in a root tree, together with the full fit result for each toy.
/// \param nRun Part of the root tree file name to facilitate parallel production.
///
void MethodPluginScan::scan2d(int nRun)
{
	cout << "MethodPluginScan::scan2d() : starting ..." << endl;

	// Set limit to all parameters.
  combiner->loadParameterLimits();

	// Define scan parameters and scan range.
  RooRealVar *par1 = w->var(scanVar1);
  assert(par1);
  float min1 = hCL2d->GetXaxis()->GetXmin();
  float max1 = hCL2d->GetXaxis()->GetXmax();
  RooRealVar *par2 = w->var(scanVar2);
  assert(par2);
  float min2 = hCL2d->GetYaxis()->GetXmin();
  float max2 = hCL2d->GetYaxis()->GetXmax();

	RooRandom::randomGenerator()->SetSeed(0);

	// Set up root tree.
	TTree *t = new TTree("plugin", "plugin");
  float scanpoint1;
  float scanpoint2;
  float chi2min;
  float chi2minGlobal_t = chi2minGlobal;
  float chi2minToy;
  float chi2minGlobalToy;
  float scanbest1;
  float scanbest2;

  t->Branch("scanpoint1", &scanpoint1, "scanpoint1/F");
  t->Branch("scanpoint2", &scanpoint2, "scanpoint2/F");
  t->Branch("chi2minGlobal", &chi2minGlobal_t, "chi2minGlobal/F");
  t->Branch("chi2min", &chi2min, "chi2min/F");
  t->Branch("chi2minToy", &chi2minToy, "chi2minToy/F");
  t->Branch("chi2minGlobalToy", &chi2minGlobalToy, "chi2minGlobalToy/F");
  t->Branch("scanbest1", &scanbest1, "scanbest1/F");
  t->Branch("scanbest2", &scanbest2, "scanbest2/F");
  t->Branch("nrun", &nRun, "nrun/F");

  map<string,float> parametersScan;
  map<string,float> parametersFree;
  map<string,float> parametersPll;
  TIterator* it2 = w->set(parsName)->createIterator();
  while ( RooRealVar* p = (RooRealVar*)it2->Next() )
  {
    parametersScan.insert(pair<string,float>(p->GetName(),p->getVal()));
    t->Branch(TString(p->GetName())+"_scan", &parametersScan[p->GetName()], TString(p->GetName())+"_scan/F");
    parametersFree.insert(pair<string,float>(p->GetName(),p->getVal()));
    t->Branch(TString(p->GetName())+"_free", &parametersFree[p->GetName()], TString(p->GetName())+"_free/F");
    parametersPll.insert(pair<string,float>(p->GetName(),p->getVal()));
    t->Branch(TString(p->GetName())+"_start", &parametersPll[p->GetName()], TString(p->GetName())+"_start/F");
  }
  delete it2;

  // add observables (toys) to ttree
  map<string,float> observablesTree;
  TIterator* it3 = w->set(obsName)->createIterator();
  while ( RooRealVar* p = (RooRealVar*)it3->Next() )
  {
    observablesTree.insert(pair<string,float>(p->GetName(),p->getVal()));
    t->Branch(TString(p->GetName()), &observablesTree[p->GetName()], TString(p->GetName())+"/F");
  }
  delete it3;

	// Save parameter values that were active at function
	// call. We'll reset them at the end to be transparent
	// to the outside.
	RooDataSet* parsFunctionCall = new RooDataSet("parsFunctionCall", "parsFunctionCall", *w->set(parsName));
	parsFunctionCall->add(*w->set(parsName));

	// for the status bar
  float nTotalSteps = nPoints2dx*nPoints2dy*nToys;
  float nStep = 0;
  float printFreq = nTotalSteps>150 ? 100 : nTotalSteps;

	//
	// 1. assume we have already the global minimum
	//

  // start scan
  for ( int i1=0; i1<nPoints2dx; i1++ )
  for ( int i2=0; i2<nPoints2dy; i2++ )
  {
    scanpoint1 = min1 + (max1-min1)*(double)i1/(double)nPoints2dx + hCL2d->GetXaxis()->GetBinWidth(1)/2.;
    scanpoint2 = min2 + (max2-min2)*(double)i2/(double)nPoints2dy + hCL2d->GetYaxis()->GetBinWidth(1)/2.;

    // don't scan in unphysical region
    if ( scanpoint1 < par1->getMin() || scanpoint1 > par1->getMax() ) continue;
    if ( scanpoint2 < par2->getMin() || scanpoint2 > par2->getMax() ) continue;

    //
    // 2. for each value of par, find best values
    //    for the remaining nuisance parameters, and the according minimal
    //    chi2. Calculate the difference to the minimum obtained in step 1.
    //

		// If we have the externally provided results for each
		// point of the profile likelihood curve, we'll use those as start
		// values.
    RooArgList *extCurveResult = 0;
    {
      int iCurveRes1 = hCL2d->GetXaxis()->FindBin(scanpoint1)-1;
      int iCurveRes2 = hCL2d->GetYaxis()->FindBin(scanpoint2)-1;
      if ( !profileLH->curveResults2d[iCurveRes1][iCurveRes2] )
      {
        printf("MethodPluginScan::scan2d() : WARNING : curve result not found, "
        "id=[%i,%i], val=[%f,%f]\n", iCurveRes1, iCurveRes2, scanpoint1, scanpoint2);
      }
      else
      {
        printf("MethodPluginScan::scan2d() : loading start parameters from external 1-CL curve: "
        "id=[%i,%i], val=[%f,%f]\n", iCurveRes1, iCurveRes2, scanpoint1, scanpoint2);
        extCurveResult = new RooArgList(profileLH->curveResults2d[iCurveRes1][iCurveRes2]->floatParsFinal());
        setParameters(w, parsName, extCurveResult);

        // check if the scan variable here differs from that of
        // the external curve
        RooArgList list = profileLH->curveResults2d[iCurveRes1][iCurveRes2]->floatParsFinal();
        list.add(profileLH->curveResults2d[iCurveRes1][iCurveRes2]->constPars());
        RooRealVar* var1 = (RooRealVar*)list.find(scanVar1);
        RooRealVar* var2 = (RooRealVar*)list.find(scanVar2);
        if ( var1 && var2 )
        {
          if ( fabs((scanpoint1-var1->getVal())/scanpoint1) > 0.01 )
            cout << "MethodPluginScan::scan2d() : WARNING : scanpoint1 and external point differ by more than 1%: "
                 << "scanpoint1=" << scanpoint1 << " var1=" << var1->getVal() << endl;
          if ( fabs((scanpoint2-var2->getVal())/scanpoint2) > 0.01 )
            cout << "MethodPluginScan::scan2d() : WARNING : scanpoint2 and external point differ by more than 1%: "
                 << "scanpoint2=" << scanpoint2 << " var2=" << var2->getVal() << endl;
        }
        else
        {
          cout << "MethodPluginScan::scan2d() : WARNING : variable 1 or 2 not found"
          ", var1=" << scanVar1 << ", var2=" << scanVar1 << endl;
          cout << "MethodPluginScan::scan2d() : Printout follows:" << endl;
          profileLH->curveResults2d[iCurveRes1][iCurveRes2]->Print();
          exit(1);
        }
      }
    }

    // set and fix scan point
    par1->setConstant(true);
    par2->setConstant(true);
    par1->setVal(scanpoint1);
    par2->setVal(scanpoint2);

    //
    // Get global minimum at scan point. This is the point in parameter space where
    // the toys need to be generated. We just fit to the minimum: If the minimum is
    // provided externally (see above) it will confirm it. If not, it will (hopefully)
    // find the global minimum on its own (dangerous!).
    //
    RooFitResult *r;
    if ( !arg->scanforce ) r = fitToMinBringBackAngles(w->pdf(pdfName), false, -1);
    else                   r = fitToMinForce(w, name);
    chi2min = r->minNll();
    delete r;

    RooDataSet* parsGlobalMinScanPoint = new RooDataSet("parsGlobalMinScanPoint", "parsGlobalMinScanPoint", *w->set(parsName));
  	parsGlobalMinScanPoint->add(*w->set(parsName));

  	// save for root tree
    TIterator* it6 = w->set(parsName)->createIterator();
    while ( RooRealVar* p = (RooRealVar*)it6->Next() ) parametersPll[p->GetName()] = p->getVal();
    delete it6;

		// check if the external minimum was found correctly
		if ( extCurveResult )
		{
		  TIterator* it = parsGlobalMinScanPoint->get(0)->createIterator();
      while ( RooRealVar* p = (RooRealVar*)it->Next() )
      {
        if ( p->GetName()==scanVar1 ) continue; // not in extCurveResult
        if ( p->GetName()==scanVar2 ) continue;
        float extVal = ((RooRealVar*)extCurveResult->find(p->GetName()))->getVal();
        float intVal = p->getVal();
        if ( fabs(extVal-intVal)/intVal>0.02 )
        {
          cout << "MethodPluginScan::scan2d() : WARNING : External and refitted minimum differ by more than 2%:" << endl;
          cout << p->GetName() << " ext=" << extVal << " int=" << intVal << endl;
        }
      }
      delete it;
		}

    // Draw toy datasets in advance. This is much faster.
    RooDataSet *toyDataSet = w->pdf(pdfName)->generate(*w->set(obsName), nToys, AutoBinned(false));

    for ( int j=0; j<nToys; j++ )
    {
      // status bar
      if (((int)nStep % (int)(nTotalSteps/printFreq)) == 0)
      {
        cout << scanpoint1 << " " << scanpoint2 << " " << ((float)nStep/(float)nTotalSteps*100.) << "%" << endl;
      }
      nStep+=1;

      //
      // 1. Load toy dataset
      //
      const RooArgSet* toyData = toyDataSet->get(j);
      setParameters(w, obsName, toyData);

      // save for root tree
      TIterator* it4 = w->set(obsName)->createIterator();
      while ( RooRealVar* p = (RooRealVar*)it4->Next() ) observablesTree[p->GetName()] = p->getVal();
      delete it4;

      //
      // 4. Fit the toy dataset to global minimum, varying all parameters.
      //
      par1->setConstant(false);
      par2->setConstant(false);
      par1->setVal(scanpoint1);
      par2->setVal(scanpoint2);
      RooFitResult *r;
      if ( !arg->scanforce ) r = fitToMinBringBackAngles(w->pdf(pdfName), false, -1);
      else                   r = fitToMinForce(w, name);
      chi2minGlobalToy = r->minNll();
      delete r;

      // save for root tree
      scanbest1 = par1->getVal();
      scanbest2 = par2->getVal();
      TIterator* it3 = w->set(parsName)->createIterator();
      while ( RooRealVar* p = (RooRealVar*)it3->Next() ) parametersFree[p->GetName()] = p->getVal();
      delete it3;

      //
      // 5. Fit the toy dataset to minimum, with par fixed to the scan value.
      //
      par1->setConstant(true);
      par2->setConstant(true);
      par1->setVal(scanpoint1);
      par2->setVal(scanpoint2);
      if ( !arg->scanforce ) r = fitToMinBringBackAngles(w->pdf(pdfName), false, -1);
      else                   r = fitToMinForce(w, name);
      chi2minToy = r->minNll();
      delete r;

      // save for tree
      TIterator* it5 = w->set(parsName)->createIterator();
      while ( RooRealVar* p = (RooRealVar*)it5->Next() ) parametersScan[p->GetName()] = p->getVal();
      delete it5;

      //
      // 6. store
      //
      t->Fill();
    }

    // reset
    setParameters(w, parsName, parsFunctionCall->get(0));
    setParameters(w, obsName, obsDataset->get(0));
    delete toyDataSet;
  }

	// save tree
	cout << "MethodPluginScan::scan2d() : saving root tree ..." << endl;
	TFile *f = new TFile(Form("root/scan2dPlugin_"+name+"_"+scanVar1+"_"+scanVar2+"_run%i.root", nRun), "recreate");
  t->Write();
  f->Close();

  // read back in to plot
  readScan2dTrees(nRun, nRun);
}

///
/// Analyse toys that were written either by a scan
/// or a by getPvalue(). Create a histogram of p-values
/// vs scanoints with as many bins for the scanpoint as
/// found in the ToyTree.
///
/// \param t    A ToyTree set up for reading (open() was called).
/// \param id   Only consider entries that have the id branch set to this value.
///             This is used e.g. by the coverage tests to distinguish the different
///             coverage toys.
///             Default is -1 which uses all entries regardless of their id.
/// \return     A new histogram that contains the p-values vs the scanpoint.
///
TH1F* MethodPluginScan::analyseToys(ToyTree* t, int id)
{
	/// \todo replace this such that there's always one bin per scan point, but still the range is the scan range.
  /// \todo Also, if we use the min/max from the tree, we have the problem that they are not exactly
  /// the scan range, so that the axis won't show the lowest and highest number.
  /// \todo If the scan range was changed after the toys were generate, we absolutely have
  /// to derive the range from the root files - else we'll have bining effects.

  float halfBinWidth = (t->getScanpointMax()-t->getScanpointMin())/(float)t->getScanpointN()/2;
  if ( t->getScanpointN()==1 ) halfBinWidth = 1.;
  TH1F *hCL 				 = new TH1F(getUniqueRootName(), "hCL", t->getScanpointN(), t->getScanpointMin()-halfBinWidth, t->getScanpointMax()+halfBinWidth);
  TH1F *h_better     = (TH1F*)hCL->Clone("h_better");
  TH1F *h_all        = (TH1F*)hCL->Clone("h_all");
  TH1F *h_background = (TH1F*)hCL->Clone("h_background");
  TH1F *h_gof        = (TH1F*)hCL->Clone("h_gof");

  Long64_t nentries  = t->GetEntries();
  Long64_t nfailed   = 0;
	Long64_t nwrongrun = 0;
  Long64_t ntoysid   = 0; ///< if id is not -1, this will count the number of toys with that id

	t->activateCoreBranchesOnly();                       ///< speeds up the event loop
	ProgressBar pb(arg, nentries);
  cout << "MethodPluginScan::analyseToys() : reading toys ..." << endl;

  for (Long64_t i = 0; i < nentries; i++)
  {
		pb.progress();
    t->GetEntry(i);
    if ( id!=-1 && fabs(t->id-id)>0.001 ) continue; ///< only select entries with given id (unless id==-1)
    ntoysid++;

    // apply cuts
    if ( ! (fabs(t->chi2minToy)<500 && fabs(t->chi2minGlobalToy)<500
         && t->statusFree==0. && t->statusScan==0. )
      ){
      nfailed++;
      continue;
    }

		// toys from a wrong run
		if ( id!=-1 && ! (fabs(t->chi2minGlobal-chi2minGlobal)<0.2) ){
      nwrongrun++;
    }

    // Cut away toys outside a certain range. This is needed to remove
    // low statistics spikes to get publication quality log plots.
    // Also check line 272 in ToyTree.cpp.
    if ( arg->pluginPlotRangeMin!=arg->pluginPlotRangeMax
      && !(arg->pluginPlotRangeMin<t->scanpoint
      && t->scanpoint<arg->pluginPlotRangeMax) ) continue;

    // use profile likelihood from internal scan, not the one found in the root files
    if ( arg->pluginext ){
      t->chi2min = profileLH->getChi2min(t->scanpoint);
    }

    // Check if toys are in physical region.
    // Don't enforce t.chi2min-t.chi2minGlobal>0, else it can be hard because due
    // to little fluctuaions the best fit point can be missing from the plugin plot...
    bool inPhysicalRegion = t->chi2minToy-t->chi2minGlobalToy>0; //&& t.chi2min-t.chi2minGlobal>0

    // build test statistic
    if ( inPhysicalRegion && t->chi2minToy-t->chi2minGlobalToy > t->chi2min-t->chi2minGlobal ){
      h_better->Fill(t->scanpoint);
    }

    // goodness-of-fit
    if ( inPhysicalRegion && t->chi2minGlobalToy > t->chi2minGlobal ){
      h_gof->Fill(t->scanpoint);
    }

    // all toys
    if ( inPhysicalRegion ){
      h_all->Fill(t->scanpoint);
    }

    // use the unphysical events to estimate background (be careful with this,
    // at least inspect the control plots to judge if this can be at all reasonable)
    if ( !inPhysicalRegion ){
      h_background->Fill(t->scanpoint);
    }
  }

	if ( id==-1 ){
		cout << "MethodPluginScan::analyseToys() : read an average of "
				 << (nentries-nfailed)/nPoints1d << " toys per scan point." << endl;
	}
	else{
		cout << "MethodPluginScan::analyseToys() : read "
				 << ntoysid << " toys at ID " << id << endl;
	}
  cout << "MethodPluginScan::analyseToys() : fraction of failed toys: " << (double)nfailed/(double)nentries*100. << "%." << endl;
  cout << "MethodPluginScan::analyseToys() : fraction of background toys: " << h_background->GetEntries()/(double)nentries*100. << "%." << endl;
  if ( id==-1 && nwrongrun>0 ){
    cout << "\nMethodPluginScan::analyseToys() : WARNING : Read toys that differ in global chi2min (wrong run) : "
          << (double)nwrongrun/(double)(nentries-nfailed)*100. << "%.\n" << endl;
  }

  for (int i=1; i<=h_better->GetNbinsX(); i++)
  {
    float nbetter = h_better->GetBinContent(i);
    float nall = h_all->GetBinContent(i);
    float nbackground = h_background->GetBinContent(i);
    if ( nall == 0. ) continue;

    // subtract background
    // float p = (nbetter-nbackground)/(nall-nbackground);
    // hCL->SetBinContent(i, p);
    // hCL->SetBinError(i, sqrt(p * (1.-p)/(nall-nbackground)));

    // don't subtract background
    float p = nbetter/nall;
		// attempt to correct for undercoverage
		if (pvalueCorrectorSet) {
			p = pvalueCorrector->transform(p);
		}
    hCL->SetBinContent(i, p);
    hCL->SetBinError(i, sqrt(p * (1.-p)/nall));
  }

  // goodness-of-fit
	if ( id==-1 ){
	  int iBinBestFit = hCL->GetMaximumBin();
		float assumedbestfitpoint = hCL->GetBinCenter(iBinBestFit);
	  float nGofBetter = h_gof->GetBinContent(iBinBestFit);
	  float nall = h_all->GetBinContent(iBinBestFit);
	  float fitprobabilityVal = nGofBetter/nall;
	  float fitprobabilityErr = sqrt(fitprobabilityVal * (1.-fitprobabilityVal)/nall);
	  cout << "MethodPluginScan::analyseToys() : fit prob of best-fit point (" << assumedbestfitpoint << "): "
	    << Form("(%.1f+/-%.1f)%%", fitprobabilityVal*100., fitprobabilityErr*100.) << endl;
	}

	t->activateAllBranches();
  delete h_better;
  delete h_all;
  delete h_background;
  return hCL;
}

///
/// Read in the TTrees that were produced by scan1d().
/// Fills the 1-CL histogram.
///
void MethodPluginScan::readScan1dTrees(int runMin, int runMax)
{
  TChain *c = new TChain("plugin");
  int nFilesMissing = 0;
  int nFilesRead = 0;
  TString dirname = "root/scan1dPlugin_"+name+"_"+scanVar1;
  TString fileNameBase = dirname+"/scan1dPlugin_"+name+"_"+scanVar1+"_run";
  for (int i=runMin; i<=runMax; i++){
    TString file = Form(fileNameBase+"%i.root", i);
    if ( !FileExists(file) ){
      if ( arg->verbose ) cout << "MethodPluginScan::readScan1dTrees() : ERROR : File not found: " + file + " ..." << endl;
      nFilesMissing += 1;
      continue;
    }
    if ( arg->verbose ) cout << "MethodPluginScan::readScan1dTrees() : reading " + file + " ..." << endl;
    c->Add(file);
    nFilesRead += 1;
  }
  cout << "MethodPluginScan::readScan1dTrees() : read files: " << nFilesRead
       << ", missing files: " << nFilesMissing
       << "                                                               "
       << "                    " << endl; // many spaces to overwrite the above \r
  cout << "MethodPluginScan::readScan1dTrees() : " << fileNameBase+"*.root" << endl;
  if ( nFilesRead==0 ){
    cout << "MethodPluginScan::readScan1dTrees() : no files read!" << endl;
    exit(1);
  }

  ToyTree t(combiner, c);
  t.open();

  if ( arg->controlplot )
  {
    if ( arg->plotid==0 || arg->plotid==1 ) t.ctrlPlotMore(profileLH);
    if ( arg->plotid==0 || arg->plotid==2 ) t.ctrlPlotSummary();
    if ( arg->plotid==0 || arg->plotid==3 ) t.ctrlPlotNuisances();
    if ( arg->plotid==0 || arg->plotid==4 ) t.ctrlPlotObservables();
    if ( arg->plotid==0 || arg->plotid==5 ) t.ctrlPlotChi2Distribution();
    if ( arg->plotid==0 || arg->plotid==6 ) t.ctrlPlotChi2Parabola();
    t.saveCtrlPlots();
  }

  if ( hCL ) delete hCL;
  hCL = analyseToys(&t, -1);
}

///
/// Read in the TTrees that were produced by scan2d().
/// Fills the 1-CL histogram.
/// \todo This is very outdated. Use Fitter and ToyTree classes at least!
/// \param runMin Number of first root file to read.
/// \param runMax Number of lase root file to read.
///
void MethodPluginScan::readScan2dTrees(int runMin, int runMax)
{
  TChain *t = new TChain("plugin");
  int nFilesMissing = 0;
  int nFilesRead = 0;
  TString fileNameBase = "root/scan2dPlugin_"+name+"_"+scanVar1+"_"+scanVar2+"_run";
  for (int i=runMin; i<=runMax; i++)
  {
    TString file = Form(fileNameBase+"%i.root", i);
    if ( !FileExists(file) )
    {
      if ( arg->verbose ) cout << "MethodPluginScan::readScan2dTrees() : ERROR : File not found: " + file + " ..." << endl;
      nFilesMissing += 1;
      continue;
    }
    if ( arg->verbose ) cout << "MethodPluginScan::readScan2dTrees() : reading " + file + " ..." << endl;
    t->Add(file);
    nFilesRead += 1;
  }
  cout << "MethodPluginScan::readScan2dTrees() : read files: " << nFilesRead << ", missing files: " << nFilesMissing << endl;
  cout << "MethodPluginScan::readScan2dTrees() : " << fileNameBase+"*.root" << endl;

  float scanpoint1;
  float scanpoint2;
  float chi2min;
  float chi2minGlobal_t;
  float chi2minToy;
  float chi2minGlobalToy;
  float scanbest1;
  float scanbest2;
  float aNuisance;

  t->SetBranchAddress("scanpoint1",        &scanpoint1);
  t->SetBranchAddress("scanpoint2",        &scanpoint2);
  t->SetBranchAddress("scanbest1",         &scanbest1);
  t->SetBranchAddress("scanbest2",         &scanbest2);
  t->SetBranchAddress("chi2min",          &chi2min);
  t->SetBranchAddress("chi2minGlobal",    &chi2minGlobal_t);
  t->SetBranchAddress("chi2minToy",       &chi2minToy);
  t->SetBranchAddress("chi2minGlobalToy", &chi2minGlobalToy);
  // t->SetBranchAddress("kD_k3pi_free",      &aNuisance);

  // new tree that has file number as leaf and all cuts applied
  TTree *tNew = 0;
  float nfile;
  float chi2minExt;
  if ( arg && arg->controlplot )
  {
    tNew = new TTree("plugin", "plugin");
    tNew->Branch("scanpoint1",        &scanpoint1,        "scanpoint1/F");
    tNew->Branch("scanpoint2",        &scanpoint2,        "scanpoint2/F");
    tNew->Branch("scanbest1",         &scanbest1,         "scanbest1/F");
    tNew->Branch("scanbest2",         &scanbest2,         "scanbest2/F");
    tNew->Branch("chi2min",          &chi2min,          "chi2min/F");
    tNew->Branch("chi2minGlobal",    &chi2minGlobal_t,  "chi2minGlobal/F");
    tNew->Branch("chi2minToy",       &chi2minToy,       "chi2minToy/F");
    tNew->Branch("chi2minGlobalToy", &chi2minGlobalToy, "chi2minGlobalToy/F");
    tNew->Branch("nfile",            &nfile,            "nfile/F");
    tNew->Branch("chi2minExt",       &chi2minExt,       "chi2minExt/F");
  }

  TH2F *h_better = (TH2F*)hCL2d->Clone("h_better");
  TH2F *h_all = (TH2F*)hCL2d->Clone("h_all");
  Long64_t nentries = t->GetEntries();
  Long64_t nfailed = 0;

  for (Long64_t i = 0; i < nentries; i++)
  {
    t->GetEntry(i);

    // apply cuts
    if ( ! (chi2minToy > -1e10 && chi2minGlobalToy > -1e10
         && chi2minToy-chi2minGlobalToy>0
         && fabs((chi2minGlobal_t-chi2minGlobal)/(chi2minGlobal_t+chi2minGlobal))<0.01 // reject files from other runs
         && chi2minToy<100
         // && aNuisance < 0.95
      ))
    {
      nfailed++;
      continue;
    }

    if ( arg && arg->controlplot )
    {
      TString filename = t->GetCurrentFile()->GetName();
      filename.ReplaceAll(fileNameBase, "");
      filename.ReplaceAll(".root", "");
      nfile = filename.Atof();    // add file number
      int iBin = profileLH->getHchisq2d()->FindBin(scanpoint1,scanpoint2);
      chi2minExt = profileLH->getHchisq2d()->GetBinContent(iBin);   // add chi2min from Prob
      tNew->Fill();
    }

    // use external chi2, not the one from the root files
    if ( arg && arg->pluginext )
    {
      int iBin = profileLH->getHchisq2d()->FindBin(scanpoint1,scanpoint2);
      chi2min = profileLH->getHchisq2d()->GetBinContent(iBin);
    }

    int scanBin = h_all->Fill(scanpoint1,scanpoint2);

    if ( chi2minToy-chi2minGlobalToy > chi2min-chi2minGlobal_t )
      h_better->SetBinContent(scanBin, h_better->GetBinContent(scanBin)+1);
  }

  cout << "MethodPluginScan::readScan2dTrees() : read an average of " << (nentries-nfailed)/nPoints2dx/nPoints2dy << " toys per scan point." << endl;
  cout << "MethodPluginScan::readScan2dTrees() : fraction of failed toys: " << (double)nfailed/(double)nentries*100. << "%." << endl;

  if ( arg && arg->controlplot )
  {
    // make control plots
    TCanvas *c1 = new TCanvas(getUniqueRootName(), "Plugin Control Plots", 1200, 900);
    c1->Divide(4,3);
    int iPad = 1;
    c1->cd(iPad++); tNew->Draw("scanpoint1:scanbest1");
    c1->cd(iPad++); tNew->Draw("scanpoint1:scanbest1", "", "colz");
    c1->cd(iPad++); tNew->Draw("scanpoint1:chi2minToy");
    c1->cd(iPad++); tNew->Draw("scanpoint1:chi2minToy", "", "colz");
    c1->cd(iPad++); tNew->Draw("scanbest1:chi2minToy");
    c1->cd(iPad++); tNew->Draw("scanbest1:chi2minToy", "", "colz");
    c1->cd(iPad++); tNew->Draw("scanpoint:chi2min");
    tNew->Draw("scanpoint1:chi2minExt", "", "same");
    ((TGraph*)(gPad->GetPrimitive("Graph")))->SetMarkerColor(kRed);
    c1->cd(iPad++); tNew->Draw("scanpoint1:chi2min", "", "colz");
    c1->cd(iPad++)->SetLogy(); tNew->Draw("chi2minToy-chi2minGlobalToy");
    c1->cd(iPad++); tNew->Draw("chi2min:nfile");
    c1->cd(iPad++); tNew->Draw("scanpoint1:chi2min-chi2minExt", "");
    c1->cd(iPad++); tNew->Draw("chi2minGlobal:nfile");
  }

  // compute 1-CL
  for (int i=1; i<=h_better->GetNbinsX(); i++)
  for (int j=1; j<=h_better->GetNbinsY(); j++)
  {
    float nbetter = h_better->GetBinContent(i,j);
    float nall = h_all->GetBinContent(i,j);
    if ( nall == 0. ) continue;
    float f = nbetter/nall;
    hCL2d->SetBinContent(i, j, f);
    hCL2d->SetBinError(i, j, sqrt(f * (1.-f)/nall));
  }
}

///
/// Importance sampling for low p-values: Returns a value
/// between 0.05 and 1 which can be used to scale down the
/// number of toys to be generated at each scan step. The
/// function is designed such that it logarithmic p-value
/// plots look nice. Below a certain p-value, 1e-4, it
/// returns 0.
/// \param pvalue The expected p-value, e.g. from the profile likelihood
/// \return Fraction between 0.1 and 1
///
double MethodPluginScan::importance(double pvalue)
{
  double f1 = 0.05;  ///< the minimum fraction we allow
  double co = 1e-5;  ///< the p-value for which we don't generate toys anymore.
  if ( pvalue<co ) return 0.0;
  double f = (1.-pvalue)/pvalue/30.;
  if ( f>1. ) return 1.;
  if ( f<f1 ) return f1;
  return f;
}