#ifndef IMATERIALRENDERCONTEXT_H
#define IMATERIALRENDERCONTEXT_H

struct VertexInputFormat_s
{
	VertexInputFormat_s()
	{
		*(u64*)this = 0;
	}

	u64 position3d        : 1; // Use 3D positions.
	u64 position2d        : 1; // Use 2D positions.
	u64 unknown1 : 1;
	u64 unknown2 : 1;
	u64 color             : 1; // Use colors (packed into single u32).
	u64 unknown3 : 1;
	u64 colorHdr          : 1; // Use HDR colors (packed into single u64).
	u64 unknown4 : 1;
	u64 normal            : 1; // Use 3D normals.
	u64 normalPacked      : 1; // Use quantized normals (packed into single u32).
	u64 biNormal          : 1; // Use bi-normals.
	u64 tangent           : 1; // Use tangents.
	u64 blendIndex        : 1; // Use blend indices.
	u64 blendWeight       : 1; // Use blend weights.
	u64 blendWeightPacked : 1; // Use packed blend weights (packed into 2x s16).
	u64 unknown5 : 1;
	u64 unknown6 : 1;
	u64 unknown7 : 1;
	u64 unknown8 : 1;
	u64 unknown9 : 1;
	u64 unknown10 : 1;
	u64 unknown11 : 1;
	u64 unknown12 : 1;
	u64 unknown13 : 1;
	u64 unknown14 : 1;
	u64 texCoordFlags     : 8; // The texture UV flags.
};

struct DirectDrawVertexParams_s
{
	// 64-bit format flags, see function Gfx_CreateInputLayout.
	// s_currentFormatFlags caches the last used format flags.
	VertexInputFormat_s vertexFormat;
	i32 vertexStructSize;
	i32 vertexBlockIndex;
	i32 vertexBufferOffset;
	i32 vertexCount;
};

enum IndexInputFormat_e : u16
{
	DD_INDEX_FORMAT_R16 = 2,
};

struct DirectDrawIndexParams_s
{
	i32 indesStart;
	i32 indexCount;
	IndexInputFormat_e formatType;
};

#endif // IMATERIALRENDERCONTEXT_H
