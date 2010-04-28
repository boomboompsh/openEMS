/*
*	Copyright (C) 2010 Thorsten Liebig (Thorsten.Liebig@gmx.de)
*
*	This program is free software: you can redistribute it and/or modify
*	it under the terms of the GNU General Public License as published by
*	the Free Software Foundation, either version 3 of the License, or
*	(at your option) any later version.
*
*	This program is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*	GNU General Public License for more details.
*
*	You should have received a copy of the GNU General Public License
*	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "openems.h"
#include <iomanip>
#include "tools/array_ops.h"
#include "FDTD/engine.h"
#include "FDTD/engine_cylinder.h"
#include "FDTD/engine_multithread.h"
#include "FDTD/operator_ext_mur_abc.h"
#include "FDTD/processvoltage.h"
#include "FDTD/processcurrent.h"
#include "FDTD/processfields_td.h"
#include <sys/time.h>
#include <time.h>

//external libs
#include "tinyxml.h"
#include "ContinuousStructure.h"

double CalcDiffTime(timeval t1, timeval t2)
{
	double s_diff = t1.tv_sec - t2.tv_sec;
	s_diff += (t1.tv_usec-t2.tv_usec)*1e-6;
	return s_diff;
}

openEMS::openEMS()
{
	FDTD_Op=NULL;
	FDTD_Eng=NULL;
	PA=NULL;
	CylinderCoords = false;
	Enable_Dumps = true;
	DebugMat = false;
	DebugOp = false;
	m_debugBox = false;
	endCrit = 1e-6;
	m_OverSampling = 4;

	m_engine = EngineType_Standard;
	m_engine_numThreads = 0;
}

openEMS::~openEMS()
{
	Reset();
}

void openEMS::Reset()
{
	if (PA) PA->DeleteAll();
	delete PA; PA=0;
	delete FDTD_Eng; FDTD_Eng=0;
	delete FDTD_Op; FDTD_Op=0;
}

//! \brief processes a command line argument
//! returns true if argument is known
//! returns false if argument is unknown
bool openEMS::parseCommandLineArgument( const char *argv )
{
	if (!argv)
		return false;

	if (strcmp(argv,"--disable-dumps")==0)
	{
		cout << "openEMS - disabling all field dumps" << endl;
		SetEnableDumps(false);
		return true;
	}
	else if (strcmp(argv,"--debug-material")==0)
	{
		cout << "openEMS - dumping material to 'material_dump.vtk'" << endl;
		DebugMaterial();
		return true;
	}
	else if (strcmp(argv,"--debug-operator")==0)
	{
		cout << "openEMS - dumping operator to 'operator_dump.vtk'" << endl;
		DebugOperator();
		return true;
	}
	else if (strcmp(argv,"--debug-boxes")==0)
	{
		cout << "openEMS - dumping boxes to 'box_dump*.vtk'" << endl;
		DebugBox();
		return true;
	}
	else if (strcmp(argv,"--engine=multithreaded")==0)
	{
		cout << "openEMS - enabled multithreading" << endl;
		m_engine = EngineType_Multithreaded;
		return true;
	}
	else if (strncmp(argv,"--numThreads=",13)==0)
	{
		m_engine_numThreads = atoi(argv+13);
		cout << "openEMS - fixed number of threads: " << m_engine_numThreads << endl;
		return true;
	}

	return false;
}

void openEMS::SetupExcitation(TiXmlElement* Excite)
{
	if (Excite==NULL)
	{
		cerr << "Can't read openEMS Excitation Settings... " << endl;
		exit(-2);
	}

	int Excit_Type=0;
	double f0=0;
	double fc=0;
	Excite->QueryIntAttribute("Type",&Excit_Type);

	unsigned int Nyquist = 0;
	switch (Excit_Type)
	{
		case 0:
			Excite->QueryDoubleAttribute("f0",&f0);
			Excite->QueryDoubleAttribute("fc",&fc);
			Nyquist = FDTD_Op->CalcGaussianPulsExcitation(f0,fc);
			break;
		case 1:
			Excite->QueryDoubleAttribute("f0",&f0);
			Nyquist = FDTD_Op->CalcSinusExcitation(f0,NrTS);
			break;
		case 2:
			Nyquist = FDTD_Op->CalcDiracPulsExcitation();
			break;
		case 3:
			Nyquist = FDTD_Op->CalcStepExcitation();
			break;
		case 10:
			Excite->QueryDoubleAttribute("f0",&f0);
			Nyquist = FDTD_Op->CalcCustomExcitation(f0,NrTS,Excite->Attribute("Function"));
			break;
	}

	if (!Nyquist)
	{
		cerr << "openEMS: excitation setup failed!!" << endl;
		exit(2);
	}
	FDTD_Op->SetNyquistNum(Nyquist);
}

int openEMS::SetupFDTD(const char* file)
{
	if (file==NULL) return -1;
	Reset();
	int bounds[6];

	time_t startTime=time(NULL);

	TiXmlDocument doc(file);
	if (!doc.LoadFile())
	{
		cerr << "openEMS: Error File-Loading failed!!! File: " << file << endl;
		exit(-1);
	}

	cout << "Read openEMS Settings..." << endl;
	TiXmlElement* openEMSxml = doc.FirstChildElement("openEMS");
	if (openEMSxml==NULL)
	{
		cerr << "Can't read openEMS ... " << endl;
		exit(-1);
	}

	TiXmlElement* FDTD_Opts = openEMSxml->FirstChildElement("FDTD");

	if (FDTD_Opts==NULL)
	{
		cerr << "Can't read openEMS FDTD Settings... " << endl;
		exit(-1);
	}
	int help=0;
	FDTD_Opts->QueryIntAttribute("NumberOfTimesteps",&help);
	if (help<0)
		NrTS=0;
	else
		NrTS = help;

	help = 0;
	FDTD_Opts->QueryIntAttribute("CylinderCoords",&help);
	if (help==1)
	{
		cout << "Using a cylinder coordinate FDTD..." << endl;
		CylinderCoords = true;
	}

	FDTD_Opts->QueryDoubleAttribute("endCriteria",&endCrit);
	if (endCrit==0)
		endCrit=1e-6;

	FDTD_Opts->QueryIntAttribute("OverSampling",&m_OverSampling);
	if (m_OverSampling<2)
		m_OverSampling=2;

	TiXmlElement* BC = FDTD_Opts->FirstChildElement("BoundaryCond");
	if (BC==NULL)
	{
		cerr << "Can't read openEMS boundary cond Settings... " << endl;
		exit(-3);
	}
	BC->QueryIntAttribute("xmin",&bounds[0]);
	BC->QueryIntAttribute("xmax",&bounds[1]);
	BC->QueryIntAttribute("ymin",&bounds[2]);
	BC->QueryIntAttribute("ymax",&bounds[3]);
	BC->QueryIntAttribute("zmin",&bounds[4]);
	BC->QueryIntAttribute("zmax",&bounds[5]);

	cout << "Read Geometry..." << endl;
	ContinuousStructure CSX;
	string EC(CSX.ReadFromXML(openEMSxml));
	if (EC.empty()==false)
	{
		cerr << EC << endl;
		return(-2);
	}

	//*************** setup operator ************//
	cout << "Create Operator..." << endl;
	if (CylinderCoords)
	{
		FDTD_Op = Operator_Cylinder::New();
		CSX.SetCoordInputType(1); //tell CSX to use cylinder-coords
	}
	else
	{
		FDTD_Op = Operator::New();
	}

	if (FDTD_Op->SetGeometryCSX(&CSX)==false) return(2);

	FDTD_Op->SetBoundaryCondition(bounds); //operator only knows about PEC and PMC, everything else is defined by extensions (see below)

	/**************************** create all operator/engine extensions here !!!! **********************************/
	//Mur-ABC, defined as extension to the operator
	for (int n=0;n<6;++n)
	{
		if (bounds[n]==2)
		{
			Operator_Ext_Mur_ABC* op_ext_mur = new Operator_Ext_Mur_ABC(FDTD_Op);
			op_ext_mur->SetDirection(n/2,n%2);
			FDTD_Op->AddExtension(op_ext_mur);
		}
	}

	FDTD_Op->CalcECOperator();

	SetupExcitation(FDTD_Opts->FirstChildElement("Excitation"));

	if (DebugMat)
	{
		FDTD_Op->DumpMaterial2File("material_dump.vtk");
	}
	if (DebugOp)
	{
		FDTD_Op->DumpOperator2File("operator_dump.vtk");
	}

	time_t OpDoneTime=time(NULL);

	FDTD_Op->ShowStat();

	cout << "Creation time for operator: " << difftime(OpDoneTime,startTime) << " s" << endl;

	//create FDTD engine
	if (CylinderCoords)
	{
		cerr << "openEMS: creating cylinder coordinate FDTD engine..." << endl;
		FDTD_Eng = Engine_Cylinder::New((Operator_Cylinder*)FDTD_Op);
	}
	else
	{
		switch (m_engine) {
		case EngineType_Multithreaded:
			FDTD_Eng = Engine_Multithread::New(FDTD_Op,m_engine_numThreads);
			break;
		default:
			FDTD_Eng = Engine::New(FDTD_Op);
			break;
		}
	}

	//*************** setup processing ************//
	cout << "Setting up processing..." << endl;
	unsigned int Nyquist = FDTD_Op->GetNyquistNum();
	PA = new ProcessingArray(Nyquist);

	double start[3];
	double stop[3];
	vector<CSProperties*> Probes = CSX.GetPropertyByType(CSProperties::PROBEBOX);
	for (size_t i=0;i<Probes.size();++i)
	{
		//only looking for one prim atm
		CSPrimitives* prim = Probes.at(i)->GetPrimitive(0);
		if (prim!=NULL)
		{
			bool acc;
			double* bnd = prim->GetBoundBox(acc,true);
			start[0]= bnd[0];start[1]=bnd[2];start[2]=bnd[4];
			stop[0] = bnd[1];stop[1] =bnd[3];stop[2] =bnd[5];
			CSPropProbeBox* pb = Probes.at(i)->ToProbeBox();
			Processing* proc = NULL;
			if (pb)
			{
				if (pb->GetProbeType()==0)
				{
					ProcessVoltage* procVolt = new ProcessVoltage(FDTD_Op,FDTD_Eng);
					procVolt->OpenFile(pb->GetName());
					proc=procVolt;
				}
				if (pb->GetProbeType()==1)
				{
					ProcessCurrent* procCurr = new ProcessCurrent(FDTD_Op,FDTD_Eng);
					procCurr->OpenFile(pb->GetName());
					proc=procCurr;
				}
				proc->SetProcessInterval(Nyquist/m_OverSampling);
				proc->DefineStartStopCoord(start,stop);
				PA->AddProcessing(proc);
			}
			else
				delete 	proc;
		}
	}

	vector<CSProperties*> DumpProps = CSX.GetPropertyByType(CSProperties::DUMPBOX);
	for (size_t i=0;i<DumpProps.size();++i)
	{
		ProcessFieldsTD* ProcTD = new ProcessFieldsTD(FDTD_Op,FDTD_Eng);
		ProcTD->SetEnable(Enable_Dumps);
		ProcTD->SetProcessInterval(Nyquist/m_OverSampling);

		//only looking for one prim atm
		CSPrimitives* prim = DumpProps.at(i)->GetPrimitive(0);
		if (prim==NULL)
			delete 	ProcTD;
		else
		{
			bool acc;
			double* bnd = prim->GetBoundBox(acc);
			start[0]= bnd[0];start[1]=bnd[2];start[2]=bnd[4];
			stop[0] = bnd[1];stop[1] =bnd[3];stop[2] =bnd[5];
			CSPropDumpBox* db = DumpProps.at(i)->ToDumpBox();
			if (db)
			{
				ProcTD->SetDumpType((ProcessFields::DumpType)db->GetDumpType());
				ProcTD->SetDumpMode((ProcessFields::DumpMode)db->GetDumpMode());
				ProcTD->SetFileType((ProcessFields::FileType)db->GetFileType());
				for (int n=0;n<3;++n)
					ProcTD->SetSubSampling(db->GetSubSampling(n),n);
				ProcTD->SetFilePattern(db->GetName());
				ProcTD->SetFileName(db->GetName());
				ProcTD->DefineStartStopCoord(start,stop);
				ProcTD->InitProcess();
				PA->AddProcessing(ProcTD);
			}
			else
				delete 	ProcTD;
		}
	}

	// dump all boxes (voltage, current, fields, ...)
	if (m_debugBox)
	{
		PA->DumpBoxes2File("box_dump_");
	}

	return 0;
}

void openEMS::RunFDTD()
{
	cout << "Running FDTD engine... this may take a while... grab a cup of coffee?!?" << endl;

	//special handling of a field processing, needed to realize the end criteria...
	ProcessFields* ProcField = new ProcessFields(FDTD_Op,FDTD_Eng);
	PA->AddProcessing(ProcField);
	double maxE=0,currE=0;

	//add all timesteps to end-crit field processing with max excite amplitude
	unsigned int maxExcite = FDTD_Op->GetMaxExcitationTimestep();
	for (unsigned int n=0;n<FDTD_Op->E_Exc_Count;++n)
		ProcField->AddStep(FDTD_Op->E_Exc_delay[n]+maxExcite);

	double change=1;
	int prevTS=0,currTS=0;
	double speed = FDTD_Op->GetNumberCells()/1e6;
	double t_diff;

	timeval currTime;
	gettimeofday(&currTime,NULL);
	timeval startTime = currTime;
	timeval prevTime= currTime;

	//*************** simulate ************//

	int step=PA->Process();
	if ((step<0) || (step>(int)NrTS)) step=NrTS;
	while ((FDTD_Eng->GetNumberOfTimesteps()<NrTS) && (change>endCrit))
	{
		FDTD_Eng->IterateTS(step);
		step=PA->Process();

		if (ProcField->CheckTimestep())
		{
			currE = ProcField->CalcTotalEnergy();
			if (currE>maxE)
				maxE=currE;
		}

//		cout << " do " << step << " steps; current: " << eng.GetNumberOfTimesteps() << endl;
		currTS = FDTD_Eng->GetNumberOfTimesteps();
		if ((step<0) || (step>(int)(NrTS - currTS))) step=NrTS - currTS;

		gettimeofday(&currTime,NULL);

		t_diff = CalcDiffTime(currTime,prevTime);
		if (t_diff>4)
		{
			currE = ProcField->CalcTotalEnergy();
			if (currE>maxE)
				maxE=currE;
			cout << "[@" << setw(8) << (int)CalcDiffTime(currTime,startTime)  <<  "s] Timestep: " << setw(12)  << currTS << " (" << setw(6) << setprecision(2) << std::fixed << (double)currTS/(double)NrTS*100.0  << "%)" ;
			cout << " with currently " << setw(6) << setprecision(1) << std::fixed << speed*(currTS-prevTS)/t_diff << " MCells/s" ;
			if (maxE)
				change = currE/maxE;
			cout << " --- Energy: ~" << setw(6) << setprecision(2) << std::scientific << currE << " (decrement: " << setw(6)  << setprecision(2) << std::fixed << fabs(10.0*log10(change)) << "dB)" << endl;
			prevTime=currTime;
			prevTS=currTS;
		}
	}

	//*************** postproc ************//
	prevTime = currTime;
	gettimeofday(&currTime,NULL);

	t_diff = CalcDiffTime(currTime,startTime);

	cout << "Time for " << FDTD_Eng->GetNumberOfTimesteps() << " iterations with " << FDTD_Op->GetNumberCells() << " cells : " << t_diff << " sec" << endl;
	cout << "Speed: " << speed*(double)FDTD_Eng->GetNumberOfTimesteps()/t_diff << " MCells/s " << endl;
}
