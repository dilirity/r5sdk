//
// Copyright (c) 2009-2010 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//

#include "NavEditor/Include/MeshLoaderPly.h"

bool rcMeshLoaderPly::load(const std::string& filename)
{
	using namespace std;

	ifstream input(filename,std::ios::binary);
	
	if (!input.is_open())
		return false;
//we expect and only support!
/*
ply
format binary_little_endian 1.0
element vertex %d
property float x
property float y
property float z
element face %d
property list uchar int vertex_index
end_header
*/
	std::string line;
	getline(input, line);
	if (line != "ply")
		return false;
	getline(input, line);
	if (line != "format binary_little_endian 1.0")
		return false;
	while (true)
	{
		input >> line;
		if (line == "element")
		{
			input >> line;
			if (line == "vertex")
			{
				input >> m_vertCount;
				m_verts.resize(m_vertCount * 3);
			}
			else if (line == "face")
			{	
				input >> m_triCount;
				m_tris.resize(m_triCount * 3);
			}
		}
		else if (line == "end_header")
		{
			break;
		}
		else
		{
			//skip rest of the line
			getline(input, line);
		}
	}

	//skip newline
	input.seekg(1, ios_base::cur);

	for (size_t i = 0; i < m_vertCount; i++)
	{
		input.read((char*)&m_verts[i].x, sizeof(float));
		input.read((char*)&m_verts[i].y, sizeof(float));
		input.read((char*)&m_verts[i].z, sizeof(float));
	}

	for (size_t i = 0; i < m_triCount; i++)
	{
		char count;
		input.read(&count, 1);
		if (count != 3)
			return false;

		input.read((char*)&m_tris[i * 3 + 0], sizeof(int));
		input.read((char*)&m_tris[i * 3 + 1], sizeof(int));
		input.read((char*)&m_tris[i * 3 + 2], sizeof(int));
	}

	// Calculate normals.
	m_normals.resize(m_triCount);
	for (int i = 0; i < m_triCount; i++)
	{
		const rdVec3D* v0 = &m_verts[m_tris[i*3]];
		const rdVec3D* v1 = &m_verts[m_tris[i*3+1]];
		const rdVec3D* v2 = &m_verts[m_tris[i*3+2]];
		rdVec3D e0, e1;
		for (int j = 0; j < 3; ++j)
		{
			e0[j] = (*v1)[j] - (*v0)[j];
			e1[j] = (*v2)[j] - (*v0)[j];
		}
		rdVec3D* n = &m_normals[i]; // math_refractor(kawe): use rdTriNormal here?
		n->x = e0.y*e1.z - e0.z*e1.y;
		n->y = e0.z*e1.x - e0.x*e1.z;
		n->z = e0.x*e1.y - e0.y*e1.x;
		float d = sqrtf(n->x*n->x + n->y*n->y + n->z*n->z);
		if (d > 0)
		{
			d = 1.0f/d;
			n->x *= d;
			n->y *= d;
			n->z *= d;
		}
	}
	
	m_filename = filename;
	return true;
}
