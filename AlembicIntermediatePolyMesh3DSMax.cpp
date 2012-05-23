#include "AlembicIntermediatePolyMesh3DSMax.h"
//#include "SceneEnumProc.h"
#include "Utility.h"
//#include "AlembicMetadataUtils.h"
//#include "AlembicPointsUtils.h"
#include "AlembicWriteJob.h"

namespace AbcA = ::Alembic::AbcCoreAbstract::ALEMBIC_VERSION_NS;
namespace AbcB = ::Alembic::Abc::ALEMBIC_VERSION_NS;
using namespace AbcA;
using namespace AbcB;

// From the SDK
// How to calculate UV's for face mapped materials.
static Point3 basic_tva[3] = { 
	Point3(0.0,0.0,0.0),Point3(1.0,0.0,0.0),Point3(1.0,1.0,0.0)
};
static Point3 basic_tvb[3] = { 
	Point3(1.0,1.0,0.0),Point3(0.0,1.0,0.0),Point3(0.0,0.0,0.0)
};
static int nextpt[3] = {1,2,0};
static int prevpt[3] = {2,0,1};

class SortableV3f : public Alembic::Abc::V3f
{
public:  
   SortableV3f()
   {
      x = y = z = 0.0f;
   }

   SortableV3f(const Alembic::Abc::V3f & other)
   {
      x = other.x;
      y = other.y;
      z = other.z;
   }
   bool operator < ( const SortableV3f & other) const
   {
      if(other.x != x)
         return other.x > x;
      if(other.y != y)
         return other.y > y;
      return other.z > z;
   }
   bool operator > ( const SortableV3f & other) const
   {
      if(other.x != x)
         return other.x < x;
      if(other.y != y)
         return other.y < y;
      return other.z < z;
   }
   bool operator == ( const SortableV3f & other) const
   {
      if(other.x != x)
         return false;
      if(other.y != y)
         return false;
      return other.z == z;
   }
};

class SortableV2f : public Alembic::Abc::V2f
{
public:  
   SortableV2f()
   {
      x = y = 0.0f;
   }

   SortableV2f(const Alembic::Abc::V2f & other)
   {
      x = other.x;
      y = other.y;
   }
   bool operator < ( const SortableV2f & other) const
   {
      if(other.x != x)
         return other.x > x;
      return other.y > y;
   }
   bool operator > ( const SortableV2f & other) const
   {
      if(other.x != x)
         return other.x < x;
      return other.y < y;
   }
   bool operator == ( const SortableV2f & other) const
   {
      if(other.x != x)
         return false;
      return other.y == y;
   }
};

// Add a normal to the list if the smoothing group bits overlap,
// otherwise create a new vertex normal in the list
void VNormal::AddNormal(Point3 &n,DWORD s) 
{   
    if (!(s&smooth) && init) 
    {     
        if (next)
        {
            next->AddNormal(n,s);     
        }
        else 
        {      
            next = new VNormal(n,s);     
        }   
    }   
    else 
    {     
        norm += n;     
        smooth |= s;     
        init = TRUE;   
    }
} 

// Retrieves a normal if the smoothing groups overlap or there is// only one in the list
Point3 &VNormal::GetNormal( DWORD s )
{   
    if (smooth&s || !next) 
    {
        return norm;   
    }
    else
    {
        return next->GetNormal(s); 
    }
} 

// Normalize each normal in the list
void VNormal::Normalize() 
{   
    VNormal *ptr = next, *prev = this;   
    while (ptr)   
    {     
        if (ptr->smooth&smooth) 
        {      
            norm += ptr->norm;      
            prev->next = ptr->next;       
            delete ptr;      
            ptr = prev->next;     
        }     
        else 
        {      
            prev = ptr;      
            ptr = ptr->next;     
        }   
    }   
    norm = ::Normalize(norm);   
    if (next) 
    {
        next->Normalize();
    }
}


void IntermediatePolyMesh3DSMax::Save(AlembicWriteJob* writeJob, TimeValue ticks, Mesh *triMesh, MNMesh* polyMesh, Matrix3& wm, Mtl* pMtl, const int nNumSamplesWritten)
{
	const bool bFirstFrame = nNumSamplesWritten == 0;


    LONG vertCount = (polyMesh != NULL) ? polyMesh->VNum()
                                       : triMesh->getNumVerts();

    // prepare the bounding box
    Alembic::Abc::Box3d bbox;

    // allocate the points and normals
    posVec.resize(vertCount);
    //Matrix3 wm = GetRef().node->GetObjTMAfterWSM(ticks);
    for(LONG i=0;i<vertCount;i++)
    {
        Point3 &maxPoint = (polyMesh != NULL) ? polyMesh->V(i)->p
                                             : triMesh->getVert(i);
        posVec[i] = ConvertMaxPointToAlembicPoint( maxPoint );
        bbox.extendBy(posVec[i]);

		//TODO: Extend the archive bounding box
        //if (writeJob)
        //{
        //    Point3 worldMaxPoint = wm * maxPoint;
        //    Imath::V3f alembicWorldPoint = ConvertMaxPointToAlembicPoint(worldMaxPoint);
        //    writeJob->GetArchiveBBox().extendBy(alembicWorldPoint);
        //}
    }

    // allocate the sample for the points
    //if(posVec.size() == 0)
    //{
    //    bbox.extendBy(Alembic::Abc::V3f(0,0,0));
    //    posVec.push_back(Alembic::Abc::V3f(FLT_MAX,FLT_MAX,FLT_MAX));
    //}



    // abort here if we are just storing points
	bool purePointCache = static_cast<bool>(writeJob->GetOption("exportPurePointCache"));
    if(purePointCache)
    {
        //if(bFirstFrame)
        //{
        //    // store a dummy empty topology
        //    mFaceCountVec.push_back(0);
        //    mFaceIndicesVec.push_back(0);
        //    //Alembic::Abc::Int32ArraySample faceCountSample(&mFaceCountVec.front(),mFaceCountVec.size());
        //    //Alembic::Abc::Int32ArraySample faceIndicesSample(&mFaceIndicesVec.front(),mFaceIndicesVec.size());
        //    //mMeshSample.setFaceCounts(faceCountSample);
        //    //mMeshSample.setFaceIndices(faceIndicesSample);
        //}

        //mMeshSchema.set(mMeshSample);
       // mNumSamples++;
        return;
    }

	// check if we support changing topology
	bool dynamicTopology = static_cast<bool>(writeJob->GetOption("exportDynamicTopology"));

	// Get the entire face count and index Count for the mesh
    LONG faceCount = (polyMesh != NULL) ? polyMesh->FNum()
                                       : triMesh->getNumFaces();

	// create an index lookup table
    sampleCount = 0;
    for(int f = 0; f < faceCount; f += 1)
    {
        int degree = (polyMesh != NULL) ? polyMesh->F(f)->deg : 3;
        sampleCount += degree;
	}

    // let's check if we have user normals
    if((bool)writeJob->GetOption("exportNormals") && (bFirstFrame || dynamicTopology))
    {
		size_t normalCount = 0;
		size_t normalIndexCount = 0;

        if (polyMesh != NULL)
        {
            polyMesh->buildNormals();
            BuildMeshSmoothingGroupNormals(*polyMesh);
        }
        if (triMesh != NULL)
        {
            triMesh->buildNormals();
            BuildMeshSmoothingGroupNormals(*triMesh);
        }

        normalVec.reserve(sampleCount);

        // Face and vertex normals.
        // In MAX a vertex can have more than one normal (but doesn't always have it).
        for (int i = 0; i < faceCount; i++) 
        {
            int degree = (polyMesh != NULL) ? polyMesh->F(i)->deg : 3;
            for (int j = degree-1; j >= 0; j--)
            {
                Point3 vertexNormal;
                if (polyMesh != NULL)
                {
                    MNNormalSpec *normalSpec = polyMesh->GetSpecifiedNormals();
                    if (normalSpec != NULL)
                    {
                        vertexNormal = normalSpec->GetNormal(i, j);
                    }
                    else
                    {
                        vertexNormal = GetVertexNormal(polyMesh, i, j, m_MeshSmoothGroupNormals);
                    }
                }
                else
                {
                    MeshNormalSpec *normalSpec = triMesh->GetSpecifiedNormals();
                    if (normalSpec != NULL)
                    {
                        vertexNormal = normalSpec->GetNormal(i, j);
                    }
                    else
                    {
                        vertexNormal = GetVertexNormal(triMesh, i, j, m_MeshSmoothGroupNormals);
                    }
                }

				normalVec.push_back(ConvertMaxNormalToAlembicNormal(vertexNormal));
                normalCount += 1;
            }
        }

        // AlembicPrintFaceData(objectMesh);

        // now let's sort the normals 
        if((bool)writeJob->GetOption("indexedNormals")) 
        {
            std::map<SortableV3f,size_t> normalMap;
            std::map<SortableV3f,size_t>::const_iterator it;
            size_t sortedNormalCount = 0;
            std::vector<Alembic::Abc::V3f> sortedNormalVec;
            normalIndexVec.reserve(normalVec.size());
            sortedNormalVec.reserve(normalVec.size());

            // loop over all normals
            for(size_t i=0;i<normalVec.size();i++)
            {
                it = normalMap.find(normalVec[i]);
				if(it != normalMap.end()){//the normal was found in the map, so store the index to normal
                    normalIndexVec.push_back((uint32_t)it->second);
					normalIndexCount++;
				}
                else {
                    normalIndexVec.push_back((uint32_t)sortedNormalCount);
					normalIndexCount++;
                    normalMap.insert(std::pair<Alembic::Abc::V3f,size_t>(normalVec[i],(uint32_t)sortedNormalCount));
                    sortedNormalVec.push_back(normalVec[i]);
					sortedNormalCount++;
                }
            }

            // use indexed normals if they use less space
            if(sortedNormalCount * sizeof(Alembic::Abc::V3f) + 
                normalIndexCount * sizeof(uint32_t) < 
                sizeof(Alembic::Abc::V3f) * normalVec.size())
            {
                normalVec = sortedNormalVec;
                //normalCount = sortedNormalCount;
            }
            else
            {
                //normalIndexCount = 0;
                normalIndexVec.clear();
            }
            //sortedNormalCount = 0;
            sortedNormalVec.clear();
        }

        ClearMeshSmoothingGroupNormals();
    }

	////////////////////////////////////////////////////////////////////////////////////////////////
   if(bFirstFrame || (dynamicTopology))
   {
      // we also need to store the face counts as well as face indices
      if(mFaceIndicesVec.size() != sampleCount || sampleCount == 0)
      {
         mFaceCountVec.resize(faceCount);
         mFaceIndicesVec.resize(sampleCount);

         int offset = 0;
         for(LONG f=0;f<faceCount;f++)
         {
            int degree = (polyMesh != NULL) ? polyMesh->F(f)->deg : 3;
            mFaceCountVec[f] = degree;
			for (int i = degree-1; i >= 0; i -= 1)
            {
                int vertIndex = (polyMesh != NULL) ? polyMesh->F(f)->vtx[i]
                                                  : triMesh->faces[f].v[i];
				mFaceIndicesVec[offset++] = vertIndex;
            }
         }

         //if(mFaceIndicesVec.size() == 0)
         //{
         //   mFaceCountVec.push_back(0);
         //   mFaceIndicesVec.push_back(0);
         //}
         //Alembic::Abc::Int32ArraySample faceCountSample(&mFaceCountVec.front(),mFaceCountVec.size());
         //Alembic::Abc::Int32ArraySample faceIndicesSample(&mFaceIndicesVec.front(),mFaceIndicesVec.size());

         //mMeshSample.setFaceCounts(faceCountSample);
         //mMeshSample.setFaceIndices(faceIndicesSample);
      }

      //Alembic::AbcGeom::ON3fGeomParam::Sample normalSample;
      //if(normalVec.size() > 0 && normalCount > 0)
      //{
      //   normalSample.setScope(Alembic::AbcGeom::kFacevaryingScope);
      //   normalSample.setVals(Alembic::Abc::N3fArraySample(&normalVec.front(),normalCount));
      //   if(normalIndexCount > 0)
      //      normalSample.setIndices(Alembic::Abc::UInt32ArraySample(&normalIndexVec.front(),normalIndexCount));
      //   mMeshSample.setNormals(normalSample);
      //}
      //else if (mNumSamples == 0 && dynamicTopology)
      //{
      //   // If we are exporting dynamic topology, then we may have normals that show up later in our scene.  The problem is that Alembic wants
      //   // your parameter to be defined at sample zero if you plan to use it even later on, so we create a dummy normal parameter here if the case
      //   // requires it
      //   normalVec.push_back(Imath::V3f(0,0,0));
      //   normalCount = 0;
      //   normalIndexVec.push_back(0);
      //   normalIndexCount = 0;
      //   normalSample.setScope(Alembic::AbcGeom::kFacevaryingScope);
      //   normalSample.setVals(Alembic::Abc::N3fArraySample(&normalVec.front(),normalCount));
      //   normalSample.setIndices(Alembic::Abc::UInt32ArraySample(&normalIndexVec.front(),normalIndexCount));
      //   mMeshSample.setNormals(normalSample);
      //}

      // also check if we need to store UV
      if((bool)writeJob->GetOption("exportUVs"))
      {
          mUvVec.reserve(sampleCount);

          if (polyMesh != NULL)
          {
              MNMap *map = polyMesh->M(1);

              for (int i=0; i<faceCount; i++) 
              {
                  int degree = polyMesh->F(i)->deg;
                  for (int j = degree-1; j >= 0; j -= 1)
                  {
                      if (map != NULL && map->FNum() > i && map->F(i)->deg > j)
                      {
                          int vertIndex = map->F(i)->tv[j];
                          UVVert texCoord = map->V(vertIndex);
                          Alembic::Abc::V2f alembicUV(texCoord.x, texCoord.y);
                          mUvVec.push_back(alembicUV);
                      }
                      else
                      {
                          Alembic::Abc::V2f alembicUV(0.0f, 0.0f);
                          mUvVec.push_back(alembicUV);
                      }
                  }
              }
          }
          else if (triMesh != NULL)
          {
              if (CheckForFaceMap(pMtl, triMesh)) 
              {
                  for (int i=0; i<faceCount; i++) 
                  {
                      Point3 tv[3];
                      Face* f = &triMesh->faces[i];
                      make_face_uv(f, tv);

                      for (int j=2; j>=0; j-=1)
                      {
                          Alembic::Abc::V2f alembicUV(tv[j].x, tv[j].y);
                          mUvVec.push_back(alembicUV);
                      }
                  }
              }
              else if (triMesh->mapSupport(1))
              {
                  MeshMap &map = triMesh->Map(1);

                  for (int findex =0; findex < map.fnum; findex += 1)
                  {
                      TVFace &texFace = map.tf[findex];
                      for (int vindex = 2; vindex >= 0; vindex -= 1)
                      {
                          int vertexid = texFace.t[vindex];
                          UVVert uvVert = map.tv[vertexid];
                          Alembic::Abc::V2f alembicUV(uvVert.x, uvVert.y);
                          mUvVec.push_back(alembicUV);
                      }
                  }
              }
          }

          if (mUvVec.size() == sampleCount)
          {
              // now let's sort the uvs 
              size_t uvCount = mUvVec.size();
              size_t uvIndexCount = 0;
              if((bool)writeJob->GetOption("indexedUVs")) 
              {
                  std::map<SortableV2f,size_t> uvMap;
                  std::map<SortableV2f,size_t>::const_iterator it;
                  size_t sortedUVCount = 0;
                  std::vector<Alembic::Abc::V2f> sortedUVVec;
                  mUvIndexVec.reserve(mUvVec.size());
                  sortedUVVec.reserve(mUvVec.size());

                  // loop over all uvs
                  for(size_t i=0; i<mUvVec.size(); i++)
                  {
                      it = uvMap.find(mUvVec[i]);
                      if(it != uvMap.end()){
                          mUvIndexVec.push_back((uint32_t)it->second);
						  uvIndexCount++;
					  }
                      else
					  {
                          mUvIndexVec.push_back((uint32_t)sortedUVCount);
						  uvIndexCount++;
                          uvMap.insert(std::pair<Alembic::Abc::V2f,size_t>(mUvVec[i],(uint32_t)sortedUVCount));
                          sortedUVVec.push_back(mUvVec[i]);
						  sortedUVCount++;
                      }
                  }

                  // use indexed uvs if they use less space
                  if(sortedUVCount * sizeof(Alembic::Abc::V2f) + 
                      uvIndexCount * sizeof(uint32_t) < 
                      sizeof(Alembic::Abc::V2f) * mUvVec.size())
                  {
                      mUvVec = sortedUVVec;
                      //uvCount = sortedUVCount;
                  }
                  else
                  {
                      //uvIndexCount = 0;
                      mUvIndexVec.clear();
                  }
                  //sortedUVCount = 0;
                  sortedUVVec.clear();
              }

              //if (mUvVec.size() > 0 && uvCount > 0)
              //{
              //    Alembic::AbcGeom::OV2fGeomParam::Sample uvSample(Alembic::Abc::V2fArraySample(&mUvVec.front(),uvCount),Alembic::AbcGeom::kFacevaryingScope);

              //    if(mUvIndexVec.size() > 0 && uvIndexCount > 0)
              //        uvSample.setIndices(Alembic::Abc::UInt32ArraySample(&mUvIndexVec.front(),uvIndexCount));
              //    mMeshSample.setUVs(uvSample);
              //}
              //else if (mNumSamples == 0 && dynamicTopology)
              //{
              //    // If we are exporting dynamic topology, then we may have uvs that show up later in our scene.  The problem is that Alembic wants
              //    // your parameter to be defined at sample zero if you plan to use it even later on, so we create a dummy uv parameter here if the case
              //    // requires it
              //    mUvVec.push_back(Imath::V2f(0,0));
              //    uvCount = 0;
              //    mUvIndexVec.push_back(0);
              //    uvIndexCount = 0;
              //    Alembic::AbcGeom::OV2fGeomParam::Sample uvSample(Alembic::Abc::V2fArraySample(&mUvVec.front(),uvCount),Alembic::AbcGeom::kFacevaryingScope);
              //    uvSample.setIndices(Alembic::Abc::UInt32ArraySample(&mUvIndexVec.front(),uvIndexCount));
              //    mMeshSample.setUVs(uvSample);
              //}
          }
      }

//TODO: finish writing out facesets

#if 0
      // sweet, now let's have a look at face sets (really only for first sample)
      // for 3DS Max, we are mapping this to the material ids
      std::vector<boost::int32_t> zeroFaceVector;
	  if(writeJob->GetOption("exportMaterialIds") && (mNumSamples == 0 || dynamicTopology))
      {
          if (polyMesh != NULL || triMesh != NULL)
          {
              if(!mMatIdProperty.valid())
              {
                  mMatIdProperty = OUInt32ArrayProperty(mMeshSchema, ".materialids", mMeshSchema.getMetaData(), writeJob->GetAnimatedTs());
              }

			  int numMatId = 0;
              int numFaces = polyMesh ? polyMesh->numf : triMesh->getNumFaces();
              mMatIdIndexVec.resize(numFaces);
              for (int i = 0; i < numFaces; i += 1)
              {
                  int matId = polyMesh ? polyMesh->f[i].material : triMesh->faces[i].getMatID();
                  mMatIdIndexVec[i] = matId;

                  // Record the face set map if sample zero
                  if (mNumSamples == 0)
                  {
                      facesetmap_it it;
                      it = mFaceSetsMap.find(matId);

                      if (it == mFaceSetsMap.end())
                      {
                          facesetmap_ret_pair ret = mFaceSetsMap.insert(facesetmap_insert_pair(matId, std::vector<int32_t>()));
                          it = ret.first;
						  numMatId++;
                      }

                      it->second.push_back(i);
                  }
              }

              size_t nMatIndexSize = mMatIdIndexVec.size();
              if(nMatIndexSize == 0)
                  mMatIdIndexVec.push_back(0);
              Alembic::Abc::UInt32ArraySample sample = Alembic::Abc::UInt32ArraySample(&mMatIdIndexVec.front(), nMatIndexSize);
              mMatIdProperty.set(sample);

			  //Mtl* pMat = GetRef().node->GetMtl();

              // For sample zero, export the material ids as face sets
			  if (mNumSamples == 0 && numMatId > 1 )
              {
				  int i = 0;
				  for ( facesetmap_it it=mFaceSetsMap.begin(); it != mFaceSetsMap.end(); it++)
                  {
                     

					  Mtl* pSubMat = NULL;
					  if(pMat && i<pMat->NumSubMtls()){
					      pSubMat = pMat->GetSubMtl(i);
					  }
					  std::stringstream nameStream;
					  int nMaterialId = it->first+1;
					  if(pSubMat){
                          nameStream<<pSubMat->GetName();
					  }
					  else if(pMat){
						  nameStream<<pMat->GetName();
					  }
					  else{
					      nameStream<<"Unnamed";
					  }
					  nameStream<<" : "<<nMaterialId;
					  std::string name = nameStream.str();

                      std::vector<int32_t> & faceSetVec = it->second;

                      Alembic::AbcGeom::OFaceSet faceSet = mMeshSchema.createFaceSet(name);
                      Alembic::AbcGeom::OFaceSetSchema::Sample faceSetSample(Alembic::Abc::Int32ArraySample(&faceSetVec.front(),faceSetVec.size()));
                      faceSet.getSchema().set(faceSetSample);

					  i++;
                  }
              }
          }
      }
#endif

      // save the sample
      //mMeshSchema.set(mMeshSample);

      // check if we need to export the bindpose (also only for first frame)
	  /*
      if(GetJob()->GetOption(L"exportBindPose") && prim.GetParent3DObject().GetEnvelopes().GetCount() > 0 && mNumSamples == 0)
      {
         mBindPoseProperty = OV3fArrayProperty(mMeshSchema, ".bindpose", mMeshSchema.getMetaData(), GetJob()->GetAnimatedTs());

         // store the positions of the modeling stack into here
         PolygonMesh bindPoseGeo = prim.GetGeometry(time, siConstructionModeModeling);
         CVector3Array bindPosePos = bindPoseGeo.GetPoints().GetPositionArray();
         mBindPoseVec.resize((size_t)bindPosePos.GetCount());
         for(LONG i=0;i<bindPosePos.GetCount();i++)
         {
            mBindPoseVec[i].x = (float)bindPosePos[i].GetX();
            mBindPoseVec[i].y = (float)bindPosePos[i].GetY();
            mBindPoseVec[i].z = (float)bindPosePos[i].GetZ();
         }

         Alembic::Abc::V3fArraySample sample;
         if(mBindPoseVec.size() > 0)
            sample = Alembic::Abc::V3fArraySample(&mBindPoseVec.front(),mBindPoseVec.size());
         mBindPoseProperty.set(sample);
      }
      */
   }
   else// the else clause for if(bFirstFrame || (dynamicTopology))
   {
      //Alembic::AbcGeom::ON3fGeomParam::Sample normalSample;
      //if(normalVec.size() > 0 && normalCount > 0)
      //{
      //   normalSample.setScope(Alembic::AbcGeom::kFacevaryingScope);
      //   normalSample.setVals(Alembic::Abc::N3fArraySample(&normalVec.front(),normalCount));
      //   if(normalIndexCount > 0)
      //      normalSample.setIndices(Alembic::Abc::UInt32ArraySample(&normalIndexVec.front(),normalIndexCount));
      //   mMeshSample.setNormals(normalSample);
      //}
      //mMeshSchema.set(mMeshSample);
   }

   // check if we should export the velocities
   /*if(dynamicTopology)
   {
      ICEAttribute velocitiesAttr = mesh.GetICEAttributeFromName(L"PointVelocity");
      if(velocitiesAttr.IsDefined() && velocitiesAttr.IsValid())
      {
         CICEAttributeDataArrayVector3f velocitiesData;
         velocitiesAttr.GetDataArray(velocitiesData);

         if(!mVelocityProperty.valid())
            mVelocityProperty = OV3fArrayProperty(mMeshSchema, ".velocities", mMeshSchema.getMetaData(), GetJob()->GetAnimatedTs());

         mVelocitiesVec.resize(vertCount);
         for(LONG i=0;i<vertCount;i++)
         {
            mVelocitiesVec[i].x = velocitiesData[i].GetX();
            mVelocitiesVec[i].y = velocitiesData[i].GetY();
            mVelocitiesVec[i].z = velocitiesData[i].GetZ();
         }

         if(mVelocitiesVec.size() == 0)
            mVelocitiesVec.push_back(Alembic::Abc::V3f(0,0,0));
         Alembic::Abc::V3fArraySample sample = Alembic::Abc::V3fArraySample(&mVelocitiesVec.front(),mVelocitiesVec.size());
         mVelocityProperty.set(sample);
      }
   }
   */

}



Point3 IntermediatePolyMesh3DSMax::GetVertexNormal(Mesh *mesh, int faceNo, int faceVertNo, std::vector<VNormal> &sgVertexNormals)
{
	// If we do not a smoothing group, we can't base ourselves on anything else,
    // so we can just return the face normal.
    Face *face = &mesh->faces[faceNo];
    if (face == NULL || face->smGroup == 0)
    {
        return mesh->getFaceNormal(faceNo);
    }

    // Check to see if there is a smoothing group normal
    int vertIndex = face->v[faceVertNo];
    Point3 normal = sgVertexNormals[vertIndex].GetNormal(face->smGroup);

    if (normal.LengthSquared() > 0.0f)
    {
        return normal.Normalize();
    }

    // If we did not find any normals or the normals offset each other for some
    // reason, let's just let max tell us what it thinks the normal should be.
    return mesh->getNormal(vertIndex);
}

Point3 IntermediatePolyMesh3DSMax::GetVertexNormal(MNMesh *mesh, int faceNo, int faceVertNo, std::vector<VNormal> &sgVertexNormals)
{
    // If we do not a smoothing group, we can't base ourselves on anything else,
    // so we can just return the face normal.
    MNFace *face = mesh->F(faceNo);
    if (face == NULL || face->smGroup == 0)
    {
        return mesh->GetFaceNormal(faceNo);
    }

    // Check to see if there is a smoothing group normal
    int vertIndex = face->vtx[faceVertNo];
    Point3 normal = sgVertexNormals[vertIndex].GetNormal(face->smGroup);

    if (normal.LengthSquared() > 0.0f)
    {
        return normal.Normalize();
    }

    // If we did not find any normals or the normals offset each other for some
    // reason, let's just let max tell us what it thinks the normal should be.
    return mesh->GetVertexNormal(vertIndex);
}

void IntermediatePolyMesh3DSMax::make_face_uv(Face *f, Point3 *tv)
{
	int na,nhid,i;
	Point3 *basetv;
	/* make the invisible edge be 2->0 */
	nhid = 2;
	if (!(f->flags&EDGE_A))  nhid=0;
	else if (!(f->flags&EDGE_B)) nhid = 1;
	else if (!(f->flags&EDGE_C)) nhid = 2;
	na = 2-nhid;
	basetv = (f->v[prevpt[nhid]]<f->v[nhid]) ? basic_tva : basic_tvb; 
	for (i=0; i<3; i++) {  
		tv[i] = basetv[na];
		na = nextpt[na];
	}
}

BOOL IntermediatePolyMesh3DSMax::CheckForFaceMap(Mtl* mtl, Mesh* mesh)
{
    if (!mtl || !mesh) {
        return FALSE;
    }

    ULONG matreq = mtl->Requirements(-1);

    // Are we using face mapping?
    if (!(matreq & MTLREQ_FACEMAP)) {
        return FALSE;
    }

    return TRUE;
}

void IntermediatePolyMesh3DSMax::BuildMeshSmoothingGroupNormals(Mesh &mesh)
{
    m_MeshSmoothGroupNormals.resize(mesh.numVerts);
    
    for (int i = 0; i < mesh.numFaces; i++) 
    {     
        Face *face = &mesh.faces[i];
        Point3 faceNormal = mesh.getFaceNormal(i);
        for (int j=0; j<3; j++) 
        {       
            m_MeshSmoothGroupNormals[face->v[j]].AddNormal(faceNormal, face->smGroup);     
        }     
    }   
    
    for (int i=0; i < mesh.numVerts; i++) 
    {     
        m_MeshSmoothGroupNormals[i].Normalize(); 
    }
}

void IntermediatePolyMesh3DSMax::BuildMeshSmoothingGroupNormals(MNMesh &mesh)
{
    m_MeshSmoothGroupNormals.resize(mesh.numv);
    
    for (int i = 0; i < mesh.numf; i++) 
    {     
        MNFace *face = &mesh.f[i];
        Point3 faceNormal = mesh.GetFaceNormal(i);
        for (int j=0; j<face->deg; j++) 
        {       
            m_MeshSmoothGroupNormals[face->vtx[j]].AddNormal(faceNormal, face->smGroup);     
        }     
    }   
    
    for (int i=0; i < mesh.numv; i++) 
    {     
        m_MeshSmoothGroupNormals[i].Normalize();   
    }
}

void IntermediatePolyMesh3DSMax::ClearMeshSmoothingGroupNormals()
{
    for (int i=0; i < m_MeshSmoothGroupNormals.size(); i++) 
    {   
        VNormal *ptr = m_MeshSmoothGroupNormals[i].next;
        while (ptr)
        {
            VNormal *tmp = ptr;
            ptr = ptr->next;
            delete tmp;
        }
    }

    m_MeshSmoothGroupNormals.clear();  
}


