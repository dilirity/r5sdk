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

#include "NavEditor/Include/MeshLoaderBsp.h"

bool rcMeshLoaderBsp::load(const std::string& /*filename*/)
{
#if 0
	//we expect lumps to be in same dir

	using namespace std;
	std::string vertex_lump_path = filename + ".0003.bsp_lump";
	auto vf=fopen(vertex_lump_path.c_str(), "rb");
	
	if (!vf)
		return false;

	fseek(vf, 0, SEEK_END);
	int fsize=ftell(vf);
	fseek(vf, 0, SEEK_SET);

	m_verts.resize(fsize / sizeof(float));
	if (m_verts[i] = fread(&m_verts[i], 4, m_verts.size(), vf) != m_verts.size())
	{
		fclose(vf);
		return false;
	}
	fclose(vf);

	//TODO: triangles


	// Calculate normals.
	m_normals.resize(m_triCount);
	for (int i = 0; i < m_triCount; i++)
	{
		const rdVec3D* v0 = &m_verts[m_tris[i*3]];
		const rdVec3D* v1 = &m_verts[m_tris[i*3+1]];
		const rdVec3D* v2 = &m_verts[m_tris[i*3+2]];

		rdTriNormal(v0, v1, v2, &m_normals[i]);
	}
	
	m_filename = filename;
	return true;
#endif
	return false;
}
