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

#include "processmodematch.h"
#include "CSFunctionParser.h"
#include "tools/array_ops.h"

ProcessModeMatch::ProcessModeMatch(Operator* op, Engine* eng) : ProcessIntegral(op, eng)
{
	for (int n=0;n<2;++n)
	{
		m_ModeParser[n] = new CSFunctionParser();
		m_ModeDist[n] = NULL;
	}
	m_dualMesh = false;

	delete[] m_Results;
	m_Results = new double[2];
}

ProcessModeMatch::~ProcessModeMatch()
{
	for (int n=0;n<2;++n)
	{
		delete m_ModeParser[n];
		m_ModeParser[n] = NULL;
	}
	Reset();
}

void ProcessModeMatch::InitProcess()
{
	if (!Enabled) return;

	if (m_ModeFieldType==1)
	{
		m_TimeShift = Op->GetTimestep()/2.0;
	}

	int Dump_Dim=0;
	for (int n=0;n<3;++n)
	{
		if (start[n]>stop[n])
		{
			unsigned int help=start[n];
			start[n]=stop[n];
			stop[n]=help;
		}

		if (stop[n]>start[n])
			++Dump_Dim;

		if (stop[n] == start[n])
			m_ny = n;
	}

	int nP = (m_ny+1)%3;
	int nPP = (m_ny+2)%3;
	m_numLines[0] = stop[nP] - start[nP] + 1;
	m_numLines[1] = stop[nPP] - start[nPP] + 1;

	if (Dump_Dim!=2)
	{
		cerr << "ProcessModeMatch::InitProcess(): Warning Mode Matching Integration Box \"" << m_filename << "\" is not a surface (found dimension: " << Dump_Dim << ")" << endl;
		SetEnable(false);
		Reset();
		return;
	}

	for (int n=0;n<2;++n)
	{
		int ny = (m_ny+n+1)%3;
		int res = m_ModeParser[n]->Parse(m_ModeFunction[ny], "x,y,z,rho,a,r,t");
		if (res >= 0)
		{
			cerr << "ProcessModeMatch::InitProcess(): Warning, an error occured parsing the mode matching function (see below) ..." << endl;
			cerr << m_ModeFunction[ny] << "\n" << string(res, ' ') << "^\n" << m_ModeParser[n]->ErrorMsg() << "\n";
			SetEnable(false);
			Reset();
		}
	}

	for (int n=0;n<2;++n)
	{
		m_ModeDist[n] = Create2DArray<double>(m_numLines);
	}

	unsigned int pos[3] = {0,0,0};
	double discLine[3] = {0,0,0};
	double gridDelta = Op->GetGridDelta();
	double var[7];
	pos[m_ny] = start[m_ny];
	discLine[m_ny] = Op->GetDiscLine(m_ny,pos[m_ny],m_dualMesh);
	double norm = 0;
	double area = 0;
	for (unsigned int posP = 0;posP<m_numLines[0];++posP)
	{
		pos[nP] = start[nP] + posP;
		discLine[nP] = Op->GetDiscLine(nP,pos[nP],m_dualMesh);
		for (unsigned int posPP = 0;posPP<m_numLines[1];++posPP)
		{
			pos[nPP] = start[nPP] + posPP;
			discLine[nPP] = Op->GetDiscLine(nPP,pos[nPP],m_dualMesh);

			var[0] = discLine[0] * gridDelta; // x
			var[1] = discLine[1] * gridDelta; // y
			var[2] = discLine[2] * gridDelta; // z
			var[3] = sqrt(discLine[0]*discLine[0] + discLine[1]*discLine[1]) * gridDelta; // rho = sqrt(x^2 + y^2)
			var[4] = atan2(discLine[1], discLine[0]); // a = atan(y,x)
			var[5] = sqrt(pow(discLine[0],2)+pow(discLine[1],2)+pow(discLine[2],2)) * gridDelta; // r
			var[6] = asin(1)-atan(var[2]/var[3]); //theta (t)

			if (m_Mesh_Type == CYLINDRICAL_MESH)
			{
				var[3] = discLine[0] * gridDelta; // rho
				var[4] = discLine[1]; // a
				var[0] = discLine[0] * cos(discLine[1]) * gridDelta; // x = r*cos(a)
				var[1] = discLine[0] * sin(discLine[1]) * gridDelta; // y = r*sin(a)
				var[5] = sqrt(pow(discLine[0],2)+pow(discLine[2],2)) * gridDelta; // r
				var[6] = asin(1)-atan(var[2]/var[3]); //theta (t)
			}
			area = Op->GetNodeArea(m_ny,pos,m_dualMesh);
			for (int n=0;n<2;++n)
			{
				m_ModeDist[n][posP][posPP] = m_ModeParser[n]->Eval(var); //calc mode template
				if ((isnan(m_ModeDist[n][posP][posPP])) || (isinf(m_ModeDist[n][posP][posPP])))
					m_ModeDist[n][posP][posPP] = 0.0;
				norm += pow(m_ModeDist[n][posP][posPP],2) * area;
			}
//			cerr << discLine[0] << " " << discLine[1] << " : " << m_ModeDist[0][posP][posPP] << " , " << m_ModeDist[1][posP][posPP] << endl;
		}
	}

	norm = sqrt(norm);
//	cerr << norm << endl;
	// normalize template function...
	for (unsigned int posP = 0;posP<m_numLines[0];++posP)
		for (unsigned int posPP = 0;posPP<m_numLines[1];++posPP)
		{
			for (int n=0;n<2;++n)
			{
				m_ModeDist[n][posP][posPP] /= norm;
			}
//			cerr << posP << " " << posPP << " : " << m_ModeDist[0][posP][posPP] << " , " << m_ModeDist[1][posP][posPP] << endl;
		}

	ProcessIntegral::InitProcess();
}

void ProcessModeMatch::Reset()
{
	ProcessIntegral::Reset();
	for (int n=0;n<2;++n)
	{
		Delete2DArray<double>(m_ModeDist[n],m_numLines);
		m_ModeDist[n] = NULL;
	}
}


void ProcessModeMatch::SetModeFunction(int ny, string function)
{
	if ((ny<0) || (ny>2)) return;
	m_ModeFunction[ny] = function;
}

void ProcessModeMatch::SetFieldType(int type)
{
	m_ModeFieldType = type;
	if ((type<0) || (type>1))
		cerr << "ProcessModeMatch::SetFieldType: Warning, unknown field type..." << endl;
}

double ProcessModeMatch::GetField(int ny, unsigned int pos[3])
{
	if (m_ModeFieldType==0)
		return GetEField(ny,pos);
	if (m_ModeFieldType==1)
		return GetHField(ny,pos);
	return 0;
}

double ProcessModeMatch::GetEField(int ny, unsigned int pos[3])
{
	if ((pos[ny]==0) || (pos[ny]==Op->GetNumberOfLines(ny)-1))
		return 0.0;
	unsigned int DownPos[] = {pos[0],pos[1],pos[2]};
	--DownPos[ny];
	double delta = Op->GetMeshDelta(ny,pos);
	double deltaDown = Op->GetMeshDelta(ny,DownPos);
	double deltaRel = delta / (delta+deltaDown);
	if (delta*deltaDown)
	{
		return (double)Eng->GetVolt(ny,pos)*(1.0-deltaRel)/delta + (double)Eng->GetVolt(ny,DownPos)/deltaDown*deltaRel;
	}
	return 0.0;
}

double ProcessModeMatch::GetHField(int ny, unsigned int pos[3])
{
	if ((pos[ny]==0) || (pos[ny]>=Op->GetNumberOfLines(ny)-1))
		return 0.0;

	unsigned int EngPos[] = {pos[0],pos[1],pos[2]};

	int nyP = (ny+1)%3;
	if (pos[nyP] >= Op->GetNumberOfLines(nyP)-1)
		return 0.0;
	int nyPP = (ny+2)%3;
	if (pos[nyPP] >= Op->GetNumberOfLines(nyPP)-1)
		return 0.0;

	double hfield = Eng->GetCurr(ny,EngPos) / Op->GetMeshDelta(ny,EngPos,true);
	EngPos[nyP]++;
	hfield += Eng->GetCurr(ny,EngPos) / Op->GetMeshDelta(ny,EngPos,true);
	EngPos[nyPP]++;
	hfield += Eng->GetCurr(ny,EngPos) / Op->GetMeshDelta(ny,EngPos,true);
	EngPos[nyP]--;
	hfield += Eng->GetCurr(ny,EngPos) / Op->GetMeshDelta(ny,EngPos,true);
	return hfield/4.0;
}


double* ProcessModeMatch::CalcMultipleIntegrals()
{
	double value = 0;
	double field = 0;
	double purity = 0;
	double area = 0;

	int nP = (m_ny+1)%3;
	int nPP = (m_ny+2)%3;

	unsigned int pos[3] = {0,0,0};
	pos[m_ny] = start[m_ny];

	for (unsigned int posP = 0;posP<m_numLines[0];++posP)
	{
		pos[nP] = start[nP] + posP;
		for (unsigned int posPP = 0;posPP<m_numLines[1];++posPP)
		{
			pos[nPP] = start[nPP] + posPP;
			area = Op->GetNodeArea(m_ny,pos,m_dualMesh);

			for (int n=0;n<2;++n)
			{
				field = GetField((m_ny+n+1)%3,pos);
				value += field * m_ModeDist[n][posP][posPP] * area;
				purity += field*field * area;
			}
		}
	}
	if (purity!=0)
		m_Results[1] = value*value/purity;
	else
		m_Results[1] = 0;
	m_Results[0] = value;
	return m_Results;
}