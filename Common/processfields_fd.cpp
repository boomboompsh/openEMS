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

#include "processfields_fd.h"
#include "Common/operator_base.h"
#include <H5Cpp.h>
#include <iomanip>
#include <sstream>
#include <string>

ProcessFieldsFD::ProcessFieldsFD(Engine_Interface_Base* eng_if) : ProcessFields(eng_if)
{
}

ProcessFieldsFD::~ProcessFieldsFD()
{
	for (size_t n = 0; n<m_FD_Fields.size(); ++n)
	{
		Delete_N_3DArray(m_FD_Fields.at(n),numLines);
	}
	m_FD_Fields.clear();
}

void ProcessFieldsFD::InitProcess()
{
	if (Enabled==false) return;

	if (m_FD_Samples.size()==0)
	{
		cerr << "ProcessFieldsFD::InitProcess: No frequencies found... skipping this dump!" << endl;
		Enabled=false;
		return;
	}

	//setup the hdf5 file
	ProcessFields::InitProcess();

	if (m_fileType==HDF5_FILETYPE)
	{
		//create	 hdf5 file & necessary groups
		m_filename+= ".h5";
		H5::H5File* file = new H5::H5File( m_filename , H5F_ACC_TRUNC );
		H5::Group* group = new H5::Group( file->createGroup( "/FieldData" ));
		delete group;
		group = new H5::Group( file->createGroup( "/FieldData/FD" ));
		delete group;
		delete file;

		//write mesh information in main root-group
		#ifdef OUTPUT_IN_DRAWINGUNITS
		double discScaling = 1;
		#else
		double discScaling = Op->GetGridDelta();
		#endif
		ProcessFields::WriteMesh2HDF5(m_filename,"/",numLines,discLines,m_Mesh_Type, discScaling);
	}

	//create data structures...
	for (size_t n = 0; n<m_FD_Samples.size(); ++n)
	{
		std::complex<float>**** field_fd = Create_N_3DArray<std::complex<float> >(numLines);
		m_FD_Fields.push_back(field_fd);
	}
}

int ProcessFieldsFD::Process()
{
	if (Enabled==false) return -1;
	if (CheckTimestep()==false) return GetNextInterval();

	if ((m_FD_Interval==0) || (m_Eng_Interface->GetNumberOfTimesteps()%m_FD_Interval!=0))
		return GetNextInterval();

	FDTD_FLOAT**** field_td = CalcField();
	std::complex<float>**** field_fd = NULL;

	double T = m_Eng_Interface->GetTime(m_dualTime);
	unsigned int pos[3];
	for (size_t n = 0; n<m_FD_Samples.size(); ++n)
	{
		std::complex<float> exp_jwt = std::exp( (std::complex<float>)(-2.0 * _I * M_PI * m_FD_Samples.at(n) * T) );
		field_fd = m_FD_Fields.at(n);
		for (pos[0]=0; pos[0]<numLines[0]; ++pos[0])
		{
			for (pos[1]=0; pos[1]<numLines[1]; ++pos[1])
			{
				for (pos[2]=0; pos[2]<numLines[2]; ++pos[2])
				{
					field_fd[0][pos[0]][pos[1]][pos[2]] += field_td[0][pos[0]][pos[1]][pos[2]] * exp_jwt;
					field_fd[1][pos[0]][pos[1]][pos[2]] += field_td[1][pos[0]][pos[1]][pos[2]] * exp_jwt;
					field_fd[2][pos[0]][pos[1]][pos[2]] += field_td[2][pos[0]][pos[1]][pos[2]] * exp_jwt;
				}
			}
		}
	}
	Delete_N_3DArray<FDTD_FLOAT>(field_td,numLines);
	++m_FD_SampleCount;
	return GetNextInterval();
}

void ProcessFieldsFD::PostProcess()
{
	DumpFDData();
}

void ProcessFieldsFD::DumpFDData()
{
#ifdef OUTPUT_IN_DRAWINGUNITS
	double discLines_scaling = 1;
#else
	double discLines_scaling = Op->GetGridDelta();
#endif

	if (m_fileType==VTK_FILETYPE)
	{
		unsigned int pos[3];
		FDTD_FLOAT**** field = Create_N_3DArray<float>(numLines);
		std::complex<float>**** field_fd = NULL;
		double angle=0;
		int Nr_Ph = 21;

		for (size_t n = 0; n<m_FD_Samples.size(); ++n)
		{
			//dump multiple phase to vtk-files
			for (int p=0; p<Nr_Ph; ++p)
			{
				angle = 2.0 * M_PI * p / Nr_Ph;
				std::complex<float> exp_jwt = std::exp( (std::complex<float>)( _I * angle) );
				field_fd = m_FD_Fields.at(n);
				for (pos[0]=0; pos[0]<numLines[0]; ++pos[0])
				{
					for (pos[1]=0; pos[1]<numLines[1]; ++pos[1])
					{
						for (pos[2]=0; pos[2]<numLines[2]; ++pos[2])
						{
							field[0][pos[0]][pos[1]][pos[2]] = real(field_fd[0][pos[0]][pos[1]][pos[2]] * exp_jwt)/(float)m_FD_SampleCount;
							field[1][pos[0]][pos[1]][pos[2]] = real(field_fd[1][pos[0]][pos[1]][pos[2]] * exp_jwt)/(float)m_FD_SampleCount;
							field[2][pos[0]][pos[1]][pos[2]] = real(field_fd[2][pos[0]][pos[1]][pos[2]] * exp_jwt)/(float)m_FD_SampleCount;
						}
					}
				}
				stringstream ss;
				ss << m_filename << fixed << "_f=" << m_FD_Samples.at(n) << "_p=" << std::setw( 3 ) << std::setfill( '0' ) <<(int)(angle * 180 / M_PI) << ".vtk";
				ofstream file(ss.str().c_str());
				if (file.is_open()==false)
					cerr << "ProcessFieldsFD::DumpFDData: can't open file '" << ss.str() << "' for writing... abort! " << endl;
				DumpVectorArray2VTK(file,GetFieldNameByType(m_DumpType),field,discLines,numLines,m_precision,string("Interpolation: ")+m_Eng_Interface->GetInterpolationTypeString(), m_Mesh_Type, discLines_scaling);
				file.close();
			}

			{
				//dump magnitude to vtk-files
				for (pos[0]=0; pos[0]<numLines[0]; ++pos[0])
				{
					for (pos[1]=0; pos[1]<numLines[1]; ++pos[1])
					{
						for (pos[2]=0; pos[2]<numLines[2]; ++pos[2])
						{
							field[0][pos[0]][pos[1]][pos[2]] = abs(field_fd[0][pos[0]][pos[1]][pos[2]])/(float)m_FD_SampleCount;
							field[1][pos[0]][pos[1]][pos[2]] = abs(field_fd[1][pos[0]][pos[1]][pos[2]])/(float)m_FD_SampleCount;
							field[2][pos[0]][pos[1]][pos[2]] = abs(field_fd[2][pos[0]][pos[1]][pos[2]])/(float)m_FD_SampleCount;
						}
					}
				}
				stringstream ss;
				ss << m_filename << fixed << "_f=" << m_FD_Samples.at(n) << "_abs" << ".vtk";
				ofstream file(ss.str().c_str());
				if (file.is_open()==false)
					cerr << "ProcessFieldsFD::DumpFDData: can't open file '" << ss.str() << "' for writing... abort! " << endl;
				DumpVectorArray2VTK(file,GetFieldNameByType(m_DumpType),field,discLines,numLines,m_precision,string("Interpolation: ")+m_Eng_Interface->GetInterpolationTypeString(), m_Mesh_Type, discLines_scaling);
				file.close();
			}

			{
				//dump phase to vtk-files
				for (pos[0]=0; pos[0]<numLines[0]; ++pos[0])
				{
					for (pos[1]=0; pos[1]<numLines[1]; ++pos[1])
					{
						for (pos[2]=0; pos[2]<numLines[2]; ++pos[2])
						{
							field[0][pos[0]][pos[1]][pos[2]] = arg(field_fd[0][pos[0]][pos[1]][pos[2]]);
							field[1][pos[0]][pos[1]][pos[2]] = arg(field_fd[1][pos[0]][pos[1]][pos[2]]);
							field[2][pos[0]][pos[1]][pos[2]] = arg(field_fd[2][pos[0]][pos[1]][pos[2]]);
						}
					}
				}
				stringstream ss;
				ss << m_filename << fixed << "_f=" << m_FD_Samples.at(n) << "_arg" << ".vtk";
				ofstream file(ss.str().c_str());
				if (file.is_open()==false)
					cerr << "ProcessFieldsFD::DumpFDData: can't open file '" << ss.str() << "' for writing... abort! " << endl;
				DumpVectorArray2VTK(file,GetFieldNameByType(m_DumpType),field,discLines,numLines,m_precision,string("Interpolation: ")+m_Eng_Interface->GetInterpolationTypeString(), m_Mesh_Type, discLines_scaling);
				file.close();
			}
		}
		Delete_N_3DArray(field,numLines);
		return;
	}

	if (m_fileType==HDF5_FILETYPE)
	{
		for (size_t n = 0; n<m_FD_Samples.size(); ++n)
		{
			stringstream ss;
			ss << "f" << n;
			DumpVectorArray2HDF5(m_filename.c_str(), "/FieldData/FD", ss.str(), m_FD_Fields.at(n),numLines,1.0/(float)m_FD_SampleCount,m_FD_Samples.at(n));
		}
		return;
	}

	cerr << "ProcessFieldsTD::Process: unknown File-Type" << endl;
}