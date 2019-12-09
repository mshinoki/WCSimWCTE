#include "WCSimRunAction.hh"
#include "WCSimRunActionMessenger.hh"

#include "G4Run.hh"
#include "G4UImanager.hh"
#include "G4VVisManager.hh"
#include "G4ios.hh"

#include "jhfNtuple.h"

#ifdef REFLEX_DICTIONARY
#include "Cintex/Cintex.h"
#endif
#include "TROOT.h"
#include "TFile.h"
#include "TTree.h"
#include "TBranch.h"
#include "TStreamerInfo.h"
#include "WCSimRootEvent.hh"
#include "WCSimRootGeom.hh"
#include "WCSimPmtInfo.hh"

#include <vector>

int pawc_[500000];                // Declare the PAWC common
struct ntupleStruct jhfNtuple;    // global, ToDo: why not use and set the class member?

WCSimRunAction::WCSimRunAction(WCSimDetectorConstruction* test, WCSimRandomParameters* rand)
  : wcsimrandomparameters(rand), useTimer(false)
{
  ntuples = 1;

  // Messenger to allow IO options
  wcsimdetector = test;
  messenger = new WCSimRunActionMessenger(this);

  useDefaultROOTout = true;  //false;  TF: ToDo, make this false WHEN flat ROOT has RooTracker trees and when FiTQun can read that in.
  wcsimrootoptions = new WCSimRootOptions();

  // By default do not try and save Rootracker interaction information
  SetSaveRooTracker(0);

}

WCSimRunAction::~WCSimRunAction()
{

}

void WCSimRunAction::BeginOfRunAction(const G4Run* /*aRun*/)
{

  fSettingsOutputTree = NULL;
  fSettingsInputTree = NULL;
  for(int i = 0; i < 3; ++i){
      WCXRotation[i] = 0;
      WCYRotation[i] = 0;
      WCZRotation[i] = 0;
      WCDetCentre[i] = 0;
  }

  WCDetRadius = 0;
  WCDetHeight = 0;

  if(wcsimdetector->GetIsNuPrism()){
    if(GetSaveRooTracker()){
      //Setup settings tree
      // Assume the NEUT file is open.
      fSettingsInputTree = (TTree*) gDirectory->Get("Settings");
      fSettingsInputTree->SetBranchAddress("NuIdfdPos",fNuPlanePos);
      fSettingsInputTree->SetBranchAddress("DetRadius",&fNuPrismRadius);
    }
    if(fSettingsInputTree){
      fSettingsOutputTree = fSettingsInputTree->CloneTree(0);
    }
    else{
      fSettingsOutputTree = new TTree("Settings","Settings");
    }

    fSettingsOutputTree->Branch("WCXRotation", WCXRotation, "WCXRotation[3]/F");
    fSettingsOutputTree->Branch("WCYRotation", WCYRotation, "WCYRotation[3]/F");
    fSettingsOutputTree->Branch("WCZRotation", WCZRotation, "WCZRotation[3]/F");
    fSettingsOutputTree->Branch("WCDetCentre", WCDetCentre, "WCDetCentre[3]/F");
    fSettingsOutputTree->Branch("WCDetRadius", &WCDetRadius, "WCDetRadius/F");
    fSettingsOutputTree->Branch("WCDetHeight", &WCDetHeight, "WCDetHeight/F");
 
  }      

  if(useTimer) {
    timer.Reset();
    timer.Start();
  }
  
  numberOfEventsGenerated = 0;
  numberOfTimesWaterTubeHit = 0;
  numberOfTimesCatcherHit = 0;

#ifdef REFLEX_DICTIONARY
  ROOT::Cintex::Cintex::Enable();
#endif

  // Needed for Root 4.00/04
  WCSimRootEvent::Class()->GetStreamerInfo()->Optimize(kFALSE);
  // MF, aug 2006 ... you never know...
  WCSimRootTrigger::Class()->GetStreamerInfo()->Optimize(kFALSE);

  // Create the Root file

  // Now controlled by the messenger
  G4String rootname = GetRootFileName();
  
  if(useDefaultROOTout){
    TFile* hfile = new TFile(rootname.c_str(),"RECREATE","WCSim ROOT file");
    hfile->SetCompressionLevel(2);
    
    // Event tree
    WCSimTree = new TTree("wcsimT","WCSim Tree");
    
    wcsimrootsuperevent = new WCSimRootEvent(); //empty list
    //  wcsimrootsuperevent->AddSubEvent(); // make at least one event
    wcsimrootsuperevent->Initialize(); // make at least one event
    Int_t branchStyle = 1; //new style by default
    TTree::SetBranchStyle(branchStyle);
    Int_t bufsize = 64000;
    
    //  TBranch *branch = tree->Branch("wcsimrootsuperevent", "Jhf2kmrootsuperevent", &wcsimrootsuperevent, bufsize,0);
    TBranch *branch = WCSimTree->Branch("wcsimrootevent", "WCSimRootEvent", &wcsimrootsuperevent, bufsize,2);
    
    // Geometry tree
    
    geoTree = new TTree("wcsimGeoT","WCSim Geometry Tree");
    wcsimrootgeom = new WCSimRootGeom();
    geoTree->Branch("wcsimrootgeom", "WCSimRootGeom", &wcsimrootgeom, bufsize,0);
    
    FillGeoTree();

    // Options tree
    optionsTree = new TTree("wcsimRootOptionsT","WCSim Options Tree");
    optionsTree->Branch("wcsimrootoptions", "WCSimRootOptions", &wcsimrootoptions, bufsize, 0);
    
    //set detector & random options
    wcsimdetector->SaveOptionsToOutput(wcsimrootoptions);
    wcsimrandomparameters->SaveOptionsToOutput(wcsimrootoptions);
     
    //Setup rooTracker tree
    if(SaveRooTracker){
      //Setup TClonesArray to store Rootracker truth info
      fVertices = new TClonesArray("NRooTrackerVtx", 10);
      fVertices->Clear();
      fNVtx = 0;
      fRooTrackerOutputTree = new TTree("fRooTrackerOutputTree","Event Vertex Truth Array");
      fRooTrackerOutputTree->Branch("NVtx",&fNVtx,"NVtx/I");
      fRooTrackerOutputTree->Branch("NRooTrackerVtx","TClonesArray", &fVertices);
    }
  }

  //TF: New Flat tree format:
  rootname.replace(rootname.find(".root"),5,"_flat.root");
  TFile* flatfile = new TFile(rootname.c_str(),"RECREATE","WCSim FLAT ROOT file");
  flatfile->SetCompressionLevel(2); //default is 1 (minimal compression)
  masterTree = new TTree("MasterTree","Main WCSim Tree");
  if(wcsimdetector->GetIsNuPrism()){
    //Already have fSettingsInputTree and branched it
    if(fSettingsInputTree){
      geomTree = fSettingsInputTree->CloneTree(0);
      geomTree->SetObject("Geometry","Geometry, Software version and generation settings");
    } else
      geomTree = new TTree("Geometry","Geometry Tree");
    
    geomTree->Branch("WCXRotation", WCXRotation, "WCXRotation[3]/F");
    geomTree->Branch("WCYRotation", WCYRotation, "WCYRotation[3]/F");
    geomTree->Branch("WCZRotation", WCZRotation, "WCZRotation[3]/F");
    geomTree->Branch("WCDetCentre", WCDetCentre, "WCDetCentre[3]/F");
    geomTree->Branch("WCDetRadius", &WCDetRadius, "WCDetRadius/F");
    geomTree->Branch("WCDetHeight", &WCDetHeight, "WCDetHeight/F");
  } else
    geomTree = new TTree("Geometry","Geometry Tree");
  // flat branches, only arrays for PMTs themselves
  // define variables in header, so I can fill them in a separate function.

  geomTree->Branch("GeometryType",geo_type_string,"GeometryType[20]/Char_t");         //example: for std::string data_ : tree->Branch(branchname.c_str(), (void*)data_->c_str(),leafdescription.c_str());
  geomTree->Branch("CylinderRadius",&cyl_radius,"CylinderRadius/Double_t");
  geomTree->Branch("CylinderLength",&cyl_length,"CylinderLength/Double_t");
  geomTree->Branch("PMTtype_ID",pmt_id_string,"PMTtype_ID[50]/Char_t");
  geomTree->Branch("PMTradius_ID",&pmt_radius_id,"PMTradius_ID/Double_t");
  geomTree->Branch("PMTtype_OD",pmt_od_string,"PMTtype_OD[50]/Char_t");
  geomTree->Branch("PMTradius_OD",&pmt_radius_od,"PMTradius_OD/Double_t");
  geomTree->Branch("numPMT_ID",&numPMT_id,"numPMT_ID/Int_t");
  geomTree->Branch("numPMT_OD",&numPMT_od,"numPMT_OD/Int_t");
  geomTree->Branch("Orientation",&orient,"Orientation/Int_t");
  geomTree->Branch("Offset_x",&offset_x,"Offset_x/Double_t");
  geomTree->Branch("Offset_y",&offset_y,"Offset_y/Double_t");
  geomTree->Branch("Offset_z",&offset_z,"Offset_z/Double_t");
  //mPMT info:
  geomTree->Branch("num_mPMT",&num_mPMT,"num_mPMT/Int_t");   //ID PMTs/n ID per mPMT
  //PMT info:
  geomTree->Branch("Tube",tube_id,"Tube[numPMT_ID]/Int_t");      //ToDo: Add OD and OD identifier
  geomTree->Branch("mPMT",mPMT_id,"mPMT[numPMT_ID]/Int_t");      //mPMT: (mPMT - mPMT_PMT) pairs
  geomTree->Branch("mPMT_pmt",mPMT_pmt_id,"mPMT_pmt[numPMT_ID]/Int_t");
  geomTree->Branch("x",tube_x,"x[numPMT_ID]/Double_t");
  geomTree->Branch("y",tube_y,"y[numPMT_ID]/Double_t");
  geomTree->Branch("z",tube_z,"z[numPMT_ID]/Double_t");
  geomTree->Branch("cylLocation",cylLocation,"cylLocation[numPMT_ID]/Int_t");  
  geomTree->Branch("direction_x",dir_x,"direction_x[numPMT_ID]/Double_t");
  geomTree->Branch("direction_y",dir_y,"direction_y[numPMT_ID]/Double_t");
  geomTree->Branch("direction_z",dir_z,"direction_z[numPMT_ID]/Double_t");
  geomTree->Branch("phi",phi,"phi[numPMT_ID]/Double_t");
  geomTree->Branch("theta",theta,"theta[numPMT_ID]/Double_t");

  //Fill Branches
  //Write Trees
  FillFlatGeoTree();

  cherenkovHitsTree = new TTree("CherenkovHits","Cherenkov Hits Tree");
  cherenkovDigiHitsTree = new TTree("CherenkovDigiHits","Cherenkov DigiHits Tree");
  tracksTree = new TTree("Tracks","Tracks Tree");
  triggerTree = new TTree("Trigger","Trigger Tree");
  eventInfoTree = new TTree("EventInfo","EventInfo Tree");

  //Will be filled in EventAction
  triggerTree->Branch("Run",&run,"Run/Int_t");
  triggerTree->Branch("Event",&event,"Event/Int_t");
  triggerTree->Branch("SubEvent",&subevent,"SubEvent/Int_t");
  triggerTree->Branch("Type",&trig_type,"Type/Int_t");
  //triggerTree->Branch("Info",trig_info,"TriggerInfo[10]/Double_t");
  triggerTree->Branch("TriggeredDigits",&trig_info,"TriggerDigits/Int_t");
  triggerTree->Branch("Length",&trig_length,"TriggerLength/Double_t");
  triggerTree->Branch("StartTime",&trig_start,"StartTime/Double_t");

  eventInfoTree->Branch("Run",&run,"Run/Int_t");
  eventInfoTree->Branch("Event",&event,"Event/Int_t");
  eventInfoTree->Branch("SubEvent",&subevent,"SubEvent/Int_t");

  evNtup = new eventNtuple; // ToDo: initialize struct with 
  eventInfoTree->Branch("InteractionMode",&(evNtup->interaction_mode),"InteractionMode/Int_t");
  eventInfoTree->Branch("VertexVolume",evNtup->vtxVolume,"VertexVolume[100]/Char_t");
  eventInfoTree->Branch("Vertex_x",&(evNtup->vtx_x),"Vertex_x/Double_t");
  eventInfoTree->Branch("Vertex_y",&(evNtup->vtx_y),"Vertex_y/Double_t");
  eventInfoTree->Branch("Vertex_z",&(evNtup->vtx_z),"Vertex_z/Double_t"); 

  tracksTree->Branch("Run",&run,"Run/Int_t");
  tracksTree->Branch("Event",&event,"Event/Int_t");
  tracksTree->Branch("SubEvent",&subevent,"SubEvent/Int_t");
  tracksTree->Branch("Ntracks",&(evNtup->nTracks),"Ntracks/Int_t");
  tracksTree->Branch("Pid",(evNtup->pid),"Pid[Ntracks]/Int_t");
  tracksTree->Branch("Flag",(evNtup->flag),"Flag[Ntracks]/Int_t");
  tracksTree->Branch("Mass",(evNtup->mass),"Mass[Ntracks]/Float_t");
  tracksTree->Branch("P",(evNtup->p),"P[Ntracks]/Float_t");
  tracksTree->Branch("Energy",(evNtup->energy),"Energy[Ntracks]/Float_t");
  tracksTree->Branch("ParentID",(evNtup->parent),"ParentID[Ntracks]/Int_t");
  tracksTree->Branch("TrackID",(evNtup->trackID),"TrackID[Ntracks]/Int_t");
  tracksTree->Branch("Time",(evNtup->time),"Time[Ntracks]/Float_t");

  tracksTree->Branch("Dirx",(evNtup->dir_x),"Dirx[Ntracks]/Float_t");
  tracksTree->Branch("Diry",(evNtup->dir_y),"Diry[Ntracks]/Float_t");
  tracksTree->Branch("Dirz",(evNtup->dir_z),"Dirz[Ntracks]/Float_t");
  tracksTree->Branch("Px",(evNtup->pdir_x),"Px[Ntracks]/Float_t");
  tracksTree->Branch("Py",(evNtup->pdir_y),"Py[Ntracks]/Float_t");
  tracksTree->Branch("Pz",(evNtup->pdir_z),"Pz[Ntracks]/Float_t");
  tracksTree->Branch("Start_x",(evNtup->start_x),"Start_x[Ntracks]/Float_t");
  tracksTree->Branch("Start_y",(evNtup->start_y),"Start_y[Ntracks]/Float_t");
  tracksTree->Branch("Start_z",(evNtup->start_z),"Start_z[Ntracks]/Float_t");
  tracksTree->Branch("Stop_x",(evNtup->stop_x),"Stop_x[Ntracks]/Float_t");
  tracksTree->Branch("Stop_y",(evNtup->stop_y),"Stop_y[Ntracks]/Float_t");
  tracksTree->Branch("Stop_z",(evNtup->stop_z),"Stop_z[Ntracks]/Float_t");
  tracksTree->Branch("Length",(evNtup->length),"Length[Ntracks]/Float_t");

  cherenkovHitsTree->Branch("Run",&run,"Run/Int_t");
  cherenkovHitsTree->Branch("Event",&event,"Event/Int_t");
  cherenkovHitsTree->Branch("SubEvent",&subevent,"SubEvent/Int_t");
  cherenkovHitsTree->Branch("NHits",&(evNtup->totalNumHits),"NHits/Int_t");   // #PMTs x #(Ch+DN)hits/PMTs
  cherenkovHitsTree->Branch("NHits_noDN",&(evNtup->totalNumHits_noNoise),"NHits_noDN/Int_t");   // #PMTs x #(Ch+DN)hits/PMTs
  cherenkovHitsTree->Branch("NPMTs",&(evNtup->numTubesHit),"NPMTs/Int_t");
  cherenkovHitsTree->Branch("NPMTs_noDN",&(evNtup->numTubesHit_noNoise),"NPMTs_noDN/Int_t");
  cherenkovHitsTree->Branch("Time",(evNtup->truetime),"Time[NHits]/Float_t");
  cherenkovHitsTree->Branch("PMT_QTot",(evNtup->totalPe),"PMT_QTot[NHits]/Int_t");
  cherenkovHitsTree->Branch("PMT_QTot_noDN",(evNtup->totalPe_noNoise),"PMT_Qtot_noDN[NHits]/Int_t");

  cherenkovHitsTree->Branch("ParentID",(evNtup->parentid),"ParentID[NHits]/Int_t");
  cherenkovHitsTree->Branch("Vector_index",(evNtup->vector_index),"Vector_index[NHits]/Int_t");
  cherenkovHitsTree->Branch("Tube",(evNtup->tubeid),"Tube[NHits]/Int_t");
  cherenkovHitsTree->Branch("mPMT",(evNtup->mPMTid),"mPMT[NHits]/Int_t");
  cherenkovHitsTree->Branch("mPMT_pmt",(evNtup->mPMT_pmtid),"mPMT_pmt[NHits]/Int_t");
  cherenkovHitsTree->Branch("TrackID",(evNtup->trackid),"TrackID[NHits]/Int_t");
  cherenkovHitsTree->Branch("PMT_x",(evNtup->tube_x),"PMT_x[NHits]/Float_t");
  cherenkovHitsTree->Branch("PMT_y",(evNtup->tube_y),"PMT_y[NHits]/Float_t");
  cherenkovHitsTree->Branch("PMT_z",(evNtup->tube_z),"PMT_z[NHits]/Float_t");
  cherenkovHitsTree->Branch("PMT_dirx",(evNtup->tube_dirx),"PMT_dirx[NHits]/Float_t");
  cherenkovHitsTree->Branch("PMT_diry",(evNtup->tube_diry),"PMT_diry[NHits]/Float_t");
  cherenkovHitsTree->Branch("PMT_dirz",(evNtup->tube_dirz),"PMT_dirz[NHits]/Float_t");

  cherenkovDigiHitsTree->Branch("Run",&run,"Run/Int_t");
  cherenkovDigiHitsTree->Branch("Event",&event,"Event/Int_t");
  cherenkovDigiHitsTree->Branch("SubEvent",&subevent,"SubEvent/Int_t");
  cherenkovDigiHitsTree->Branch("NDigiHits",&(evNtup->totalNumDigiHits),"NDigiHits/Int_t");
  cherenkovDigiHitsTree->Branch("NDigiPMTs",&(evNtup->numDigiTubesHit),"NDigiPMTs/Int_t");
  cherenkovDigiHitsTree->Branch("QTotDigi",&(evNtup->sumq),"QTotDigi/Float_t");
  cherenkovDigiHitsTree->Branch("Q",(evNtup->q),"Q[NDigiHits]/Float_t");
  cherenkovDigiHitsTree->Branch("T",(evNtup->t),"T[NDigiHits]/Float_t");
  cherenkovDigiHitsTree->Branch("Vector_index",(evNtup->digivector_index),"Vector_index[NDigiHits]/Int_t");
  cherenkovDigiHitsTree->Branch("Tube",(evNtup->digitubeid),"Tube[NDigiHits]/Int_t");
  cherenkovDigiHitsTree->Branch("mPMT",(evNtup->digimPMTid),"mPMT[NDigiHits]/Int_t");
  cherenkovDigiHitsTree->Branch("mPMT_pmt",(evNtup->digimPMT_pmtid),"mPMT_pmt[NDigiHits]/Int_t");
  cherenkovDigiHitsTree->Branch("PMT_x",(evNtup->digitube_x),"PMT_x[NDigiHits]/Float_t");
  cherenkovDigiHitsTree->Branch("PMT_y",(evNtup->digitube_y),"PMT_y[NDigiHits]/Float_t");
  cherenkovDigiHitsTree->Branch("PMT_z",(evNtup->digitube_z),"PMT_z[NDigiHits]/Float_t");
  cherenkovDigiHitsTree->Branch("PMT_dirx",(evNtup->digitube_dirx),"PMT_dirx[NDigiHits]/Float_t");
  cherenkovDigiHitsTree->Branch("PMT_diry",(evNtup->digitube_diry),"PMT_diry[NDigiHits]/Float_t");
  cherenkovDigiHitsTree->Branch("PMT_dirz",(evNtup->digitube_dirz),"PMT_dirz[NDigiHits]/Float_t");

  /* TF TODO: Adapt to Flat Tree output!!
  // Options tree
  optionsTree = new TTree("wcsimRootOptionsT","WCSim Options Tree");
  optionsTree->Branch("wcsimrootoptions", "WCSimRootOptions", &wcsimrootoptions, bufsize, 0);
  
  //set detector & random options
  wcsimdetector->SaveOptionsToOutput(wcsimrootoptions);
  wcsimrandomparameters->SaveOptionsToOutput(wcsimrootoptions);
  */

  //Setup rooTracker tree
  if(SaveRooTracker){
    //Setup TClonesArray to store Rootracker truth info
    //fVertices = new TClonesArray("NRooTrackerVtx", 10);
    //fVertices->Clear();
    //fNVtx = 0;
    evNRooTracker = new NRooTrackerVtx();   // should be an array? Not clear where in WCSim NVtx > 1
    flatRooTrackerTree = new TTree("RooTracker","Event Vertex Truth Array");
    flatRooTrackerTree->Branch("Run",&run,"Run/Int_t");
    flatRooTrackerTree->Branch("Event",&event,"Event/Int_t");
    flatRooTrackerTree->Branch("SubEvent",&subevent,"SubEvent/Int_t");
    flatRooTrackerTree->Branch("NVtx",&fNVtx,"NVtx/Int_t");
    //flat branching
    flatRooTrackerTree->Branch("NuFluxEntry",&evNRooTracker->NuFluxEntry,"NuFluxEntry/Long_t");
    flatRooTrackerTree->Branch("NuFileName",&evNRooTracker->NuFileName,"NuFileName[100]/Char_t"); //CAREFUL
    flatRooTrackerTree->Branch("NuParentDecMode",&evNRooTracker->NuParentDecMode,"NuParentDecMod/Int_t");
    flatRooTrackerTree->Branch("NuParentPdg",&evNRooTracker->NuParentPdg,"NuParentPdg/Int_t");
    //// WORK IN PROGRESS
    double      NuParentDecP4 [4]; 
    double      NuParentDecX4 [4]; 
    float       NuCospibm;         
    float       NuNorm;            
    double      NuParentProP4 [4]; 
    double      NuParentProX4 [4]; 
    float       NuCospi0bm;        
    float       NuRnu;             
    float       NuXnu [2];         
    int         NuIdfd;            
    int         NuGipart;          
    float       NuGpos0[3];        
    float       NuGvec0[3];        
    float       NuGamom0;          
    int         NuNg;             
    float       NuGp[kNgmax][3];  
    float       NuGcosbm[kNgmax]; 
    float       NuGv[kNgmax][3];  
    int         NuGpid[kNgmax];   
    int         NuGmec[kNgmax];   
    float       NuEnusk;          
    float       NuNormsk;         
    float       NuAnorm;          
    int         NuGmat[kNgmax];   
    float       NuGdistc[kNgmax]; 
    float       NuGdistal[kNgmax];
    float       NuGdistti[kNgmax];
    float       NuGdistfe[kNgmax];
    float       NuVersion;        
    int         NuTuneid;         
    int         NuNtrig;          
    int         NuPint;           
    float       NuBpos[2];        
    float       NuBtilt[2];       
    float       NuBrms[2];        
    float       NuEmit[2];        
    float       NuAlpha[2];       
    float       NuHcur[3];        
    int         NuRand;           
    TObjString* EvtCode;       
    int         EvtNum;        
    double      EvtXSec;       
    double      EvtDXSec;      
    double      EvtWght;              
    double      EvtProb;              
    double      EvtVtx[4];            
    int         StdHepN;              
    int*        StdHepPdg; //[StdHepN]
    int         StdHepPdgTemp   [kNStdHepNPmax];
    int*        StdHepStatus; //[StdHepN] 
    int         StdHepStatusTemp[kNStdHepNPmax];   
    double      StdHepX4    [kNStdHepNPmax][4]; 
    double      StdHepP4    [kNStdHepNPmax][4]; 
    double      StdHepPolz  [kNStdHepNPmax][3]; 
    int  StdHepFdTemp    [kNStdHepNPmax];   
    int  StdHepLdTemp    [kNStdHepNPmax];   
    int  StdHepFmTemp    [kNStdHepNPmax];   
    int  StdHepLmTemp    [kNStdHepNPmax];   
    int*  StdHepFd;    //[StdHepN] 
    int*  StdHepLd;    //[StdHepN] 
    int*  StdHepFm;    //[StdHepN] 
    int*  StdHepLm;    //[StdHepN] 
    int    NEnvc;                    
    int    NEipvcTemp[kNEmaxvc];     
    int*   NEipvc; //[NEnvc]         
    float  NEpvc[kNEmaxvc][3];       
    int    NEiorgvcTemp[kNEmaxvc];   
    int*   NEiorgvc; //[NEnvc]       
    int    NEiflgvcTemp[kNEmaxvc];   
    int    NEicrnvcTemp[kNEmaxvc];   
    int*    NEiflgvc; 
    int*    NEicrnvc; 
    float NEcrsx;    
    float NEcrsy;    
    float NEcrsz;    
    float NEcrsphi;    
    int    NEnvert;                    
    float  NEposvert[kNEmaxvert][3];   
    int    NEiflgvertTemp[kNEmaxvert]; 
    int*   NEiflgvert;
    int    NEnvcvert;                   
    float  NEdirvert[kNEmaxvertp][3];   
    float  NEabspvertTemp[kNEmaxvertp]; 
    float  NEabstpvertTemp[kNEmaxvertp];
    int    NEipvertTemp[kNEmaxvertp];   
    int    NEivertiTemp[kNEmaxvertp];   
    int    NEivertfTemp[kNEmaxvertp];   
    float*  NEabspvert; 
    float*  NEabstpvert;
    int*    NEipvert;   
    int*    NEiverti;   
    int*    NEivertf;   
    TObjString* GeomPath;
    TObjString* GeneratorName;
    TObjString* OrigFileName; 
    TObjString* OrigTreeName; 
    int OrigEvtNum;
    int OrigTreeEntries;
    double OrigTreePOT;
    double TimeInSpill;
    int TruthVertexID;

  











































  }
  
}

void WCSimRunAction::EndOfRunAction(const G4Run*)
{
//G4cout << "Number of Events Generated: "<< numberOfEventsGenerated << G4endl;
//G4cout<<"Number of times waterTube hit: " << numberOfTimesWaterTubeHit<<G4endl;
//   G4cout << ((float(numberOfTimesMRDHit)+float(numberOfTimesFGDHit))/float(numberOfEventsGenerated))*100.
// 	 << "% hit FGD or MRD" << G4endl;
//  G4cout << (float(numberOfTimesCatcherHit)/float(numberOfEventsGenerated))*100.
//        << "% through-going (hit Catcher)" << G4endl;


  //Write the options tree
  G4cout << "EndOfRunAction" << G4endl;
  
  // Close the Root file at the end of the run

  TFile *file = masterTree->GetCurrentFile();
  file->cd();
  masterTree->AddFriend("Tracks");
  tracksTree->Write();
  masterTree->AddFriend("CherenkovHits");
  cherenkovHitsTree->Write();
  masterTree->AddFriend("CherenkovDigiHits");
  cherenkovDigiHitsTree->Write();
  masterTree->AddFriend("Trigger");
  triggerTree->Write();
  masterTree->AddFriend("EventInfo");
  eventInfoTree->Write();
  if(SaveRooTracker){
    masterTree->AddFriend("RooTracker");
    flatRooTrackerTree->Write();
  }
  //  fSettingsOutputTree->Write(); // not a friend
  //}

  masterTree->Write();
  file->Close();

  delete evNtup;


  if(useDefaultROOTout){

    // Close the Root file at the end of the run
    TFile* hfile = WCSimTree->GetCurrentFile();
    optionsTree->Fill();
    optionsTree->Write();
    hfile->Write();
    hfile->Close();
  
    // Clean up stuff on the heap; I think deletion of hfile and trees
    // is taken care of by the file close
    delete wcsimrootsuperevent; wcsimrootsuperevent=0;
    delete wcsimrootgeom; wcsimrootgeom=0;
  }


  if(useTimer) {
    timer.Stop();
    G4cout << "WCSimRunAction ran from BeginOfRunAction() to EndOfRunAction() in:"
	   << "\t" << timer.CpuTime()  << " seconds (CPU)"
	   << "\t" << timer.RealTime() << " seconds (real)" << G4endl;
  }
}

void WCSimRunAction::FillGeoTree(){

  // Fill the geometry tree
  G4int geo_type;
  G4double cylinfo[3];
  G4double pmtradius;
  G4int numpmt;
  G4int orientation;
  Float_t offset[3];
 
  Float_t rotation[3];

  Int_t tubeNo;
  Int_t mPMTNo = -1;
  Int_t mPMT_pmtNo = -1;
  Float_t pos[3];
  Float_t rot[3];
  Int_t cylLoc;

  if (wcsimdetector->GetIsEggShapedHyperK()) {
      geo_type = 2;
  }
  else {
      geo_type = 0;
  }
  // geo_type 1 is for defunct mailbox design

  wcsimrootgeom-> SetGeo_Type(geo_type);

  if (geo_type == 0) {
      //cylinder
      cylinfo[1] = wcsimdetector->GetGeo_Dm(3);
      cylinfo[2] = wcsimdetector->GetGeo_Dm(2);
      wcsimrootgeom-> SetWCCylRadius(cylinfo[1]);
      wcsimrootgeom-> SetWCCylLength(cylinfo[2]);
  }


  pmtradius = wcsimdetector->GetPMTSize1();
  numpmt = wcsimdetector->GetTotalNumPmts();
  orientation = 0;
  
  wcsimrootgeom-> SetWCPMTRadius(pmtradius);
  wcsimrootgeom-> SetOrientation(orientation);
  
  G4ThreeVector offset1= wcsimdetector->GetWCOffset();
  offset[0] = offset1[0];
  offset[1] = offset1[1];
  offset[2] = offset1[2];
  wcsimrootgeom-> SetWCOffset(offset[0],offset[1],offset[2]);

  if(wcsimdetector->GetIsNuPrism()){ 
      G4ThreeVector rotation1= wcsimdetector->GetWCXRotation();
      WCXRotation[0] = rotation1[0];
      WCXRotation[1] = rotation1[1];
      WCXRotation[2] = rotation1[2];
    
      G4ThreeVector rotation2= wcsimdetector->GetWCYRotation();
      WCYRotation[0] = rotation2[0];
      WCYRotation[1] = rotation2[1];
      WCYRotation[2] = rotation2[2];
    
      G4ThreeVector rotation3= wcsimdetector->GetWCZRotation();
      WCZRotation[0] = rotation3[0];
      WCZRotation[1] = rotation3[1];
      WCZRotation[2] = rotation3[2];

      WCDetCentre[0] = offset1[0]/100.0;
      WCDetCentre[1] = offset1[1]/100.0;
      WCDetCentre[2] = offset1[2]/100.0;

      WCDetRadius = wcsimdetector->GetWCIDDiameter()/2.0;
      WCDetHeight = wcsimdetector->GetWCIDHeight();

      if(fSettingsInputTree){
          fSettingsInputTree->GetEntry(0);
          double z_offset = fNuPlanePos[2]/100.0 + fNuPrismRadius;
          WCDetCentre[2] += z_offset;
          std::cout << "WCDetCentre[2] = " << WCDetCentre[2] << std::endl;
      }

      fSettingsOutputTree->Fill();
  }

  std::vector<WCSimPmtInfo*> *fpmts = wcsimdetector->Get_Pmts();
  WCSimPmtInfo *pmt;
  for (unsigned int i=0;i!=fpmts->size();i++){
    pmt = ((WCSimPmtInfo*)fpmts->at(i));
    pos[0] = pmt->Get_transx();
    pos[1] = pmt->Get_transy();
    pos[2] = pmt->Get_transz();
    rot[0] = pmt->Get_orienx();
    rot[1] = pmt->Get_orieny();
    rot[2] = pmt->Get_orienz();
    tubeNo = pmt->Get_tubeid();
    mPMTNo = pmt->Get_mPMTid();
    mPMT_pmtNo = pmt->Get_mPMT_pmtid();
    cylLoc = pmt->Get_cylocation();
    wcsimrootgeom-> SetPMT(i,tubeNo,mPMTNo, mPMT_pmtNo, cylLoc,rot,pos);
  }
  if (fpmts->size() != (unsigned int)numpmt) {
    G4cout << "Mismatch between number of pmts and pmt list in geofile.txt!!"<<G4endl;
    G4cout << fpmts->size() <<" vs. "<< numpmt <<G4endl;
  }
  
  wcsimrootgeom-> SetWCNumPMT(numpmt);
  
  geoTree->Fill();
  TFile* hfile = geoTree->GetCurrentFile();
  //hfile->Write(); 
  if(wcsimdetector->GetIsNuPrism()) fSettingsOutputTree->Write();
//  if(fSettingsInputTree) fSettingsInputTree->Close();

  geoTree->Write(); 
}

void WCSimRunAction::FillFlatGeoTree(){

  if (wcsimdetector->GetIsEggShapedHyperK())
    strcpy(geo_type_string,"EggShape");
  else strcpy(geo_type_string,"Cylinder");
  
  if (strcmp(geo_type_string,"Cylinder") == 0) {
    cyl_radius = wcsimdetector->GetGeo_Dm(3);
    cyl_length = wcsimdetector->GetGeo_Dm(2);
  }
  
  pmt_radius_id = wcsimdetector->GetPMTSize1();
  pmt_radius_od = 0;                                //ToDo
  numPMT_id = wcsimdetector->GetTotalNumPmts();
  numPMT_od = 0;                                    //ToDo
  orient = 0;

  strcpy(pmt_id_string,wcsimdetector->GetPMTtype_ID().c_str());
  strcpy(pmt_od_string,wcsimdetector->GetPMTtype_OD().c_str());

  G4ThreeVector offset1= wcsimdetector->GetWCOffset();
  offset_x = offset1[0];
  offset_y = offset1[1];
  offset_z = offset1[2];

  num_mPMT = numPMT_id/wcsimdetector->GetmPMT_nID();
  
  std::vector<WCSimPmtInfo*> *fpmts = wcsimdetector->Get_Pmts();
  WCSimPmtInfo *pmt;
  for (unsigned int i=0;i!=fpmts->size();i++){
    pmt = ((WCSimPmtInfo*)fpmts->at(i));
    tube_x[i] = pmt->Get_transx();
    tube_y[i] = pmt->Get_transy();
    tube_z[i] = pmt->Get_transz();
    dir_x[i] = pmt->Get_orienx();
    dir_y[i] = pmt->Get_orieny();
    dir_z[i] = pmt->Get_orienz();
    theta[i] = acos(dir_z[i]);
    phi[i] = atan2(dir_y[i],dir_x[i]);
    cylLocation[i] = UNDEFINED; // ToDo: pmt->Get_cylocation() incorrect for mPMT case

    tube_id[i] = pmt->Get_tubeid();
    mPMT_id[i] = tube_id[i]/wcsimdetector->GetmPMT_nID();
    mPMT_pmt_id[i] = (tube_id[i]%wcsimdetector->GetmPMT_nID() == 0 ? wcsimdetector->GetmPMT_nID() : tube_id[i]%wcsimdetector->GetmPMT_nID() ); // No. 1 to nID
    //cylLoc = pmt->Get_cylocation();
  }

  if (fpmts->size() != (unsigned int)numPMT_id) {
    G4cout << "Mismatch between number of pmts and pmt list in geofile.txt!!"<<G4endl;
    G4cout << fpmts->size() <<" vs. "<< numPMT_id <<G4endl;
  }

  // Merge in nuPRISM settings tree in Geom, which is most natural place
  if(wcsimdetector->GetIsNuPrism()){ 
    G4ThreeVector rotation1= wcsimdetector->GetWCXRotation();
    WCXRotation[0] = rotation1[0];
    WCXRotation[1] = rotation1[1];
    WCXRotation[2] = rotation1[2];
    
    G4ThreeVector rotation2= wcsimdetector->GetWCYRotation();
    WCYRotation[0] = rotation2[0];
    WCYRotation[1] = rotation2[1];
    WCYRotation[2] = rotation2[2];
    
    G4ThreeVector rotation3= wcsimdetector->GetWCZRotation();
    WCZRotation[0] = rotation3[0];
    WCZRotation[1] = rotation3[1];
    WCZRotation[2] = rotation3[2];
    
    WCDetCentre[0] = offset1[0]/100.0;
    WCDetCentre[1] = offset1[1]/100.0;
    WCDetCentre[2] = offset1[2]/100.0;
    
    WCDetRadius = wcsimdetector->GetWCIDDiameter()/2.0;
    WCDetHeight = wcsimdetector->GetWCIDHeight();
 
    if(fSettingsInputTree){
      fSettingsInputTree->GetEntry(0);
      double z_offset = fNuPlanePos[2]/100.0 + fNuPrismRadius;
      WCDetCentre[2] += z_offset;
      std::cout << "WCDetCentre[2] = " << WCDetCentre[2] << std::endl;
    }
  } 

  geomTree->Fill();
  TFile* flatfile = geomTree->GetCurrentFile();
  flatfile->Write(); 
}

NRooTrackerVtx* WCSimRunAction::GetRootrackerVertex(){

  NRooTrackerVtx* currRootrackerVtx = new((*fVertices)[fNVtx])NRooTrackerVtx();
  fNVtx += 1;
  return currRootrackerVtx;
}

