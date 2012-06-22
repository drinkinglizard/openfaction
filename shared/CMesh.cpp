/*****************************************************************************
*
*  PROJECT:     Open Faction
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        shared/CMesh.cpp
*  PURPOSE:     
*  DEVELOPERS:  Rafal Harabien
*
*****************************************************************************/

#include "CMesh.h"
#include "v3d_format.h"
#include "CConsole.h"
#include "CLevelProperties.h"
#include "CMeshMgr.h"
#include "CMaterialsMgr.h"
#include "CGame.h"
#include "CObject.h"
#ifdef OF_CLIENT
# include "irr/CReadFile.h"
# include "CLevel.h"
#endif // OF_CLIENT

#define ALIGN(num, alignment) (((num) + (alignment) - 1) - ((num) + (alignment) - 1) % (alignment))

using namespace std;
#ifdef OF_CLIENT
using namespace irr;
#endif // OF_CLIENT

CMesh::CMesh(CMeshMgr *pMeshMgr):
    m_pMultiColSphere(NULL), m_pMeshMgr(pMeshMgr) {}

CMesh::~CMesh()
{
    Unload();
    if(m_pMeshMgr)
        m_pMeshMgr->Remove(this);
}

int CMesh::Load(CInputBinaryStream &Stream)
{
    Unload();
    
    v3d_header_t Hdr = {0, 0, 0};
    Stream.ReadBinary(&Hdr, sizeof(Hdr));
    
    if(Hdr.signature != V3M_SIGNATURE && Hdr.signature != V3C_SIGNATURE)
    {
        m_pMeshMgr->GetGame()->GetConsole()->DbgPrint("Wrong V3D signature: 0x%x\n", Hdr.signature);
        return -1;
    }
    
    if(Hdr.version != V3D_VERSION)
        m_pMeshMgr->GetGame()->GetConsole()->DbgPrint("Unknown V3D version: 0x%x\n", Hdr.version);
    
    v3d_section_t Sect;
    while(Stream.good())
    {
        // Read section header
        streampos nPos = Stream.tellg();
        Stream.ReadBinary(&Sect, sizeof(Sect));
        
        // Is it ending section?
        if(Sect.type == V3D_END)
            break;
        
        //m_pMeshMgr->GetGame()->GetConsole()->DbgPrint("Section 0x%x at 0x%x\n", Sect.type, nPos);
        
        switch(Sect.type)
        {
            case V3D_SUBMESH:
            {
                CSubMesh *pSubMesh = new CSubMesh(m_pMeshMgr);
                pSubMesh->Load(Stream);
                m_SubMeshes.push_back(pSubMesh);
                break;
            }
            case V3D_COLSPHERE:
                LoadColSphere(Stream);
                break;
            case V3D_BONE:
                LoadBones(Stream);
                break;
            default:
                //m_pMeshMgr->GetGame()->GetConsole()->DbgPrint("Unknown section 0x%x\n", Sect.type);
                Stream.ignore(Sect.size);
        }
    }
    
    assert(Stream);
    return 0;
}

void CMesh::Unload()
{
    for(unsigned i = 0; i < m_SubMeshes.size(); ++i)
        delete m_SubMeshes[i];
    m_SubMeshes.clear();
    
    delete m_pMultiColSphere;
    m_pMultiColSphere = NULL;
}

btMultiSphereShape *CMesh::GetMultiColSphere()
{
    if(!m_pMultiColSphere && !m_ColSpheresPos.empty())
        m_pMultiColSphere = new btMultiSphereShape(&m_ColSpheresPos.front(), &m_ColSpheresRadius.front(), m_ColSpheresPos.size());
    
    return m_pMultiColSphere;
}

void CMesh::LoadColSphere(CInputBinaryStream &Stream)
{
    // Read colsphere frm stream
    Stream.ignore(24); // name
    Stream.ReadInt32(); // unknown
    btVector3 vPos = Stream.ReadVector();
    float fRadius = Stream.ReadFloat();
    assert(fRadius >= 0.0f);
    
    // Add colsphere to internal arrays
    m_ColSpheresPos.push_back(vPos);
    m_ColSpheresRadius.push_back(fRadius);
}

void CMesh::LoadBones(CInputBinaryStream &Stream)
{
    int Root = -1;
    
    unsigned cBones = Stream.ReadInt32();
    for(unsigned i = 0; i < cBones; ++i)
    {
        SBone Bone;
        char szName[25];
        
        Stream.read(szName, 24);
        szName[24] = 0;
        Bone.strName = szName;
        Bone.qRot = Stream.ReadQuaternion();
        Bone.vPos = Stream.ReadVector();
        Bone.iParent = Stream.ReadInt32();
        
        if(Bone.iParent == -1)
            Root = i;
        
        m_Bones.push_back(Bone);
    }
    
    assert(Root >= 0);
    
    for(unsigned i = 0; i < m_Bones.size(); ++i)
        assert(m_Bones[i].iParent == -1 || (m_Bones[i].iParent >= 0 && m_Bones[i].iParent < m_Bones.size()));
    
    //FixBones(Root);
}

void CMesh::FixBones(int iParent)
{
    for(unsigned i = 0; i < m_Bones.size(); ++i)
    {
        if(m_Bones[i].iParent == iParent)
        {
            btMatrix3x3 matRot(m_Bones[iParent].qRot);
            m_Bones[i].vPos = matRot * m_Bones[i].vPos;
            m_Bones[i].vPos += m_Bones[iParent].vPos;
            FixBones(i);
        }
    }
}

#ifdef OF_CLIENT
void CMesh::DbgDraw(const CObject *pObj) const
{
    video::IVideoDriver *pVideoDrv = m_pMeshMgr->GetGame()->GetVideoDriver();
    const btVector3 &vPos = pObj->GetPos();
    core::vector3df vIrrPos(vPos[0], vPos[1], vPos[2]);
    
    if((m_pMeshMgr->GetGame()->GetCamera()->getPosition() - vIrrPos).getLengthSQ() > 1000.0f)
        return;
    
    //pVideoDrv->draw3DLine(vIrrPos, core::vector3df(0, 0, 0));
    pVideoDrv->draw3DBox(core::aabbox3df(vIrrPos - core::vector3df(1, 1, 1), vIrrPos + core::vector3df(1, 1, 1)));
    for(unsigned i = 0; i < m_Bones.size(); ++i)
    {
        const SBone &Bone = m_Bones[i];
        core::vector3df vBonePos(Bone.vPos[0], Bone.vPos[1], Bone.vPos[2]);
        vBonePos += vIrrPos;
        pVideoDrv->draw3DBox(core::aabbox3df(vBonePos - core::vector3df(0.01, 0.01, 0.01), vBonePos + core::vector3df(0.01, 0.01, 0.01)), video::SColor(128, 255, 255, 0));
        
        if(Bone.iParent >= 0)
        {
            const SBone &ParentBone = m_Bones[Bone.iParent];
            core::vector3df vParentPos(ParentBone.vPos[0], ParentBone.vPos[1], ParentBone.vPos[2]);
            vParentPos += vIrrPos;
            pVideoDrv->draw3DLine(vBonePos, vParentPos, video::SColor(128, 0, 255, 0));
        }
        
        core::position2di vPosScr = m_pMeshMgr->GetGame()->GetSceneMgr()->getSceneCollisionManager()->getScreenCoordinatesFrom3DPosition(vBonePos, m_pMeshMgr->GetGame()->GetCamera());
        wchar_t wszBuf[32];
        swprintf(wszBuf, sizeof(wszBuf), L"%hs", Bone.strName.c_str());
        m_pMeshMgr->GetGame()->GetGuiEnv()->getBuiltInFont()->draw(wszBuf, core::recti(vPosScr, vPosScr), video::SColor(255, 255, 255, 255), true, true);
    }
}
#endif // OF_CLIENT

CSubMesh::CSubMesh(CMeshMgr *pMeshMgr):
    m_pMeshMgr(pMeshMgr),
    m_pColMesh(NULL), m_pColShape(NULL)
#ifdef OF_CLIENT
    , m_pIrrMesh(NULL)
#endif // OF_CLIENT
{}

CSubMesh::~CSubMesh()
{
    // Cleanup Bullet collision mesh and shape
    if(m_pColMesh)
        delete m_pColMesh;
    m_pColMesh = NULL;
    
    if(m_pColShape)
        delete m_pColShape;
    m_pColShape = NULL;
    
#ifdef OF_CLIENT
    // Cleanup Irrlicht mesh
    if(m_pIrrMesh)
        m_pIrrMesh->drop();
    m_pIrrMesh = NULL;
#endif // OF_CLIENT
    
    // Cleanup materials
    for(unsigned i = 0; i < m_Materials.size(); ++i)
        m_Materials[i]->Release();
    m_Materials.clear();
}

int CSubMesh::Load(CInputBinaryStream &Stream)
{
    Stream.ignore(24 + 24); // name, unknown
    
    uint32_t uVer = Stream.ReadUInt32();
    assert(uVer >= 7);
    
    uint32_t cLodModels = Stream.ReadUInt32();
    float fPrevDist;
    for(unsigned i = 0; i < cLodModels; ++i)
    {
        float fDist = Stream.ReadFloat();// LOD distances
        if(i == 0)
            assert(fDist == 0.0f);
        else
            assert(fDist >= fPrevDist); // some models has 3 x 0.0f (see grab.v3c)
        fPrevDist = fDist;
    }
    
    btVector3 vCenter = Stream.ReadVector();
    float fRadius = Stream.ReadFloat();
    assert(fRadius > 0.0f);
    
    btVector3 vAabb1 = Stream.ReadVector();
    btVector3 vAabb2 = Stream.ReadVector();
    assert(vAabb1.x() < vAabb2.x() && vAabb1.y() < vAabb2.y() && vAabb1.z() < vAabb2.z());
    
    for(unsigned i = 0; i < cLodModels; ++i)
    {
        //CConsole::GetInst().DbgPrint("LOD model %u at 0x%x\n", i, Stream.tellg());
        bool bColMesh = (i == cLodModels - 1); // use last model as collision model
        bool bIrrMesh = (i == 0); // use first model for rendering
        LoadLodModel(Stream, bColMesh, bIrrMesh);
    }
    
    uint32_t cMaterials = Stream.ReadUInt32();
    Stream.ignore(cMaterials * sizeof(v3d_material_t));
    
    uint32_t cUnknown = Stream.ReadUInt32();
    Stream.ignore(cUnknown * 28); // unknown4
    
    assert(Stream);
    return 0;
}

int CSubMesh::LoadLodModel(CInputBinaryStream &Stream, bool bColMesh, bool bIrrMesh)
{
    uint32_t uFlags = Stream.ReadUInt32();
    assert(uFlags <= 0x23);
    
    uint32_t cVertices = Stream.ReadUInt32();
    
    uint16_t cBatches = Stream.ReadUInt16();
    assert(cBatches > 0 && cBatches < 256);
    
    uint32_t cbData = Stream.ReadUInt32();
    assert(cbData > cBatches * 56 && cbData < 0x1000000); // 1MB
    
    //CConsole::GetInst().DbgPrint("Flags 0x%x cVertices %u cBatches %u\n", uFlags, cVertices, cBatches);
    
    unsigned uDataOffset = Stream.tellg();
    //CConsole::GetInst().DbgPrint("Data starts at 0x%x\n", uDataOffset);
    
    char *pData = new char[cbData];
    Stream.ReadBinary(pData, cbData);
    
    //CConsole::GetInst().DbgPrint("Data ends at 0x%x\n", Stream.tellg());
    
    int32_t Unknown = Stream.ReadInt32(); // unknown
    //assert(Unknown == -1);
    
    v3d_batch_info_t Batches[cBatches];
    Stream.ReadBinary(Batches, sizeof(Batches));
    for(unsigned i = 0; i < cBatches; ++i)
    {
        assert(Batches[i].positions_size >= Batches[i].vertices_count * 3 * sizeof(float));
        assert(Batches[i].tex_coords_size >= Batches[i].vertices_count * 2 * sizeof(float));
        assert(Batches[i].indices_size >= Batches[i].triangles_count * 4 * sizeof(uint16_t));
    }
    
    uint32_t Unknown2 = Stream.ReadUInt32(); // unknown2 (0, 1)
    
    uint32_t cTextures = Stream.ReadUInt32();
    //CConsole::GetInst().DbgPrint("Textures %u at 0x%x\n", cTextures, Stream.tellg());
    //assert(cTextures <= cBatches);
    if(bIrrMesh)
        assert(m_Materials.empty());
    for(unsigned i = 0; i < cTextures; ++i)
    {
        //CConsole::GetInst().DbgPrint("Texture %u at 0x%x\n", i, Stream.tellg());
        
        uint8_t Id = Stream.ReadUInt8();
        //assert(Id < cBatches);
        //assert(Id == i);
        std::string strFilename = Stream.ReadString();
        if(bIrrMesh)
        {
            CMaterial *pMaterial = m_pMeshMgr->GetGame()->GetMaterialsMgr()->Load(strFilename);
            if(!pMaterial)
                m_pMeshMgr->GetGame()->GetConsole()->DbgPrint("Failed to load texture %s\n", strFilename.c_str());
            m_Materials.push_back(pMaterial);
        }
    }
    
    if(bColMesh || bIrrMesh)
    {
        unsigned uOffset = ALIGN(cBatches * 56, V3D_ALIGNMENT);
        
#ifdef OF_CLIENT
        if(bIrrMesh)
        {
            assert(!m_pIrrMesh);
            m_pIrrMesh = new scene::SMesh;
        }
#endif // OF_CLIENT
        
        if(bColMesh)
        {
            assert(!m_pColMesh);
            m_pColMesh = new btTriangleMesh();
        }
        
        for(unsigned i = 0; i < cBatches; ++i)
        {
            assert(uOffset < cbData);
            
#ifdef OF_CLIENT
            scene::SMeshBuffer *pIrrMeshBuf = NULL;
            if(bIrrMesh)
            {
                /* Create new buffer */
                pIrrMeshBuf = new scene::SMeshBuffer;
                ((scene::SMesh*)m_pIrrMesh)->addMeshBuffer(pIrrMeshBuf);
                
                pIrrMeshBuf->Material.setFlag(video::EMF_LIGHTING, false);
                pIrrMeshBuf->Material.FogEnable = m_pMeshMgr->GetGame()->GetLevel()->GetProperties()->IsFogEnabled();
                
                if(i < cTextures)
                {
                    CMaterial *pMaterial = m_Materials[i];
                    video::ITexture *pTexture = pMaterial->GetFrame(0);
                    assert(pTexture);
                    pIrrMeshBuf->Material.setTexture(0, pTexture);
                    
                    if(pMaterial->HasAlpha())
                        pIrrMeshBuf->Material.MaterialType = video::EMT_TRANSPARENT_ALPHA_CHANNEL;
                } else
                    m_pMeshMgr->GetGame()->GetConsole()->DbgPrint("Invalid texture index\n");
                
                /* Reallocate buffers to speed up things */
                pIrrMeshBuf->Vertices.reallocate(Batches[i].vertices_count);
                pIrrMeshBuf->Indices.reallocate(Batches[i].triangles_count * 3);
            }
#endif // OF_CLIENT
            //m_pMeshMgr->GetGame()->GetConsole()->DbgPrint("Batch %u: vertices %u triangles %u possize 0x%x idxsize 0x%x, texcsize 0x%x\n", i,
            //                             Batches[i].vertices_count, Batches[i].triangles_count, Batches[i].positions_size, Batches[i].indices_size, Batches[i].tex_coords_size);
            
            //m_pMeshMgr->GetGame()->GetConsole()->DbgPrint("Positions offset 0x%x\n", uDataOffset + uOffset);
            float *pPositions = (float*)(pData + uOffset);
            uOffset = ALIGN(uOffset + Batches[i].positions_size, V3D_ALIGNMENT);
            
            //m_pMeshMgr->GetGame()->GetConsole()->DbgPrint("Normals offset 0x%x\n", uDataOffset + uOffset);
            float *pNormals = (float*)(pData + uOffset);
            uOffset = ALIGN(uOffset + Batches[i].positions_size, V3D_ALIGNMENT);
            
            //m_pMeshMgr->GetGame()->GetConsole()->DbgPrint("Tex coords offset 0x%x\n", uDataOffset + uOffset);
            float *pTexCoords = (float*)(pData + uOffset);
            uOffset = ALIGN(uOffset + Batches[i].tex_coords_size, V3D_ALIGNMENT);
            
            //m_pMeshMgr->GetGame()->GetConsole()->DbgPrint("Indices offset 0x%x\n", uDataOffset + uOffset);
            uint16_t *pIndices = (uint16_t*)(pData + uOffset);
            uOffset = ALIGN(uOffset + Batches[i].indices_size, V3D_ALIGNMENT);
            
            if(uFlags & 0x20)
                uOffset = ALIGN(uOffset + Batches[i].triangles_count * 4 * sizeof(float), V3D_ALIGNMENT);
            
            //m_pMeshMgr->GetGame()->GetConsole()->DbgPrint("Unknown offset 0x%x\n", uDataOffset + uOffset);
            uOffset = ALIGN(uOffset + Batches[i].unknown_size, V3D_ALIGNMENT);
            
            //m_pMeshMgr->GetGame()->GetConsole()->DbgPrint("Unknown2 offset 0x%x\n", uDataOffset + uOffset);
            uOffset = ALIGN(uOffset + Batches[i].unknown2_size, V3D_ALIGNMENT);
            
            //m_pMeshMgr->GetGame()->GetConsole()->DbgPrint("Unknown3 offset 0x%x\n", uDataOffset + uOffset);
            if(uFlags & 0x1)
                uOffset = ALIGN(uOffset + cVertices * 2, V3D_ALIGNMENT);
            
            for(unsigned j = 0; j < Batches[i].vertices_count; ++j)
            {
#ifdef OF_CLIENT
                if(bIrrMesh)
                {
                    video::S3DVertex Vertex;
                    Vertex.Pos.X = pPositions[j * 3 + 0];
                    Vertex.Pos.Y = pPositions[j * 3 + 1];
                    Vertex.Pos.Z = pPositions[j * 3 + 2];
                    Vertex.Normal.X = pNormals[j * 3 + 0];
                    Vertex.Normal.Y = pNormals[j * 3 + 1];
                    Vertex.Normal.Z = pNormals[j * 3 + 2];
                    Vertex.TCoords.X = pTexCoords[j * 2 + 0];
                    Vertex.TCoords.Y = pTexCoords[j * 2 + 1];
                    Vertex.Color = video::SColor(255, 255, 255, 255);
                    pIrrMeshBuf->Vertices.push_back(Vertex);
                }
#endif // OF_CLIENT
            }
            
            for(unsigned j = 0; j < Batches[i].triangles_count; ++j)
            {
                uint16_t idx1 = pIndices[j * 4 + 0], idx2 = pIndices[j * 4 + 1], idx3 = pIndices[j * 4 + 2];
                
                if(idx1 >= Batches[i].vertices_count || idx2 >= Batches[i].vertices_count || idx3 >= Batches[i].vertices_count)
                    m_pMeshMgr->GetGame()->GetConsole()->DbgPrint("Invalid triangle %u %u %u\n", idx1, idx2, idx3);
                
                assert(idx1 < Batches[i].vertices_count);
                assert(idx2 < Batches[i].vertices_count);
                assert(idx3 < Batches[i].vertices_count);
                
                if(bColMesh)
                {
                    btVector3 vPt1(pPositions[idx1 * 3 + 0], pPositions[idx1 * 3 + 1], pPositions[idx1 * 3 + 2]);
                    btVector3 vPt2(pPositions[idx2 * 3 + 0], pPositions[idx2 * 3 + 1], pPositions[idx2 * 3 + 2]);
                    btVector3 vPt3(pPositions[idx3 * 3 + 0], pPositions[idx3 * 3 + 1], pPositions[idx3 * 3 + 2]);
                    
                    m_pColMesh->addTriangle(vPt1, vPt2, vPt3);
                }
#ifdef OF_CLIENT
                if(bIrrMesh)
                {
                    pIrrMeshBuf->Indices.push_back(idx1);
                    pIrrMeshBuf->Indices.push_back(idx2);
                    pIrrMeshBuf->Indices.push_back(idx3);
                }
#endif // OF_CLIENT
            }
#ifdef OF_CLIENT
            if(bIrrMesh)
            {
                //CConsole::GetInst().DbgPrint("Buffer vertices %u indices %u\n", pIrrMeshBuf->Vertices.size(), pIrrMeshBuf->Indices.size());
                pIrrMeshBuf->recalculateBoundingBox();
                pIrrMeshBuf->drop();
            }
                
#endif // OF_CLIENT
        }
        
        if(bColMesh)
        {
            assert(!m_pColShape);
            m_pColShape = new btBvhTriangleMeshShape(m_pColMesh, true);
        }
        
#ifdef OF_CLIENT
        if(bIrrMesh)
            ((scene::SMesh*)m_pIrrMesh)->recalculateBoundingBox();
#endif // OF_CLIENT
    }
    
    delete[] pData;
    
    assert(Stream);
    return 0;
}