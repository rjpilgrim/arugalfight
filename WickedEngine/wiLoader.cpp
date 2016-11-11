#include "wiLoader.h"
#include "wiResourceManager.h"
#include "wiHelper.h"
#include "wiMath.h"
#include "wiRenderer.h"
#include "wiEmittedParticle.h"
#include "wiHairParticle.h"
#include "wiRenderTarget.h"
#include "wiDepthTarget.h"
#include "wiTextureHelper.h"
#include "wiPHYSICS.h"
#include "wiArchive.h"

using namespace wiGraphicsTypes;

void LoadWiArmatures(const string& directory, const string& name, const string& identifier, list<Armature*>& armatures)
{
	stringstream filename("");
	filename<<directory<<name;

	ifstream file(filename.str().c_str());
	if(file){
		while(!file.eof()){
			float trans[] = { 0,0,0,0 };
			string line="";
			file>>line;
			if(line[0]=='/' && line.substr(2,8)=="ARMATURE") {
				armatures.push_back(new Armature(line.substr(11,strlen(line.c_str())-11),identifier) );
			}
			else{
				switch(line[0]){
				case 'r':
					file>>trans[0]>>trans[1]>>trans[2]>>trans[3];
					armatures.back()->rotation_rest = XMFLOAT4(trans[0],trans[1],trans[2],trans[3]);
					break;
				case 's':
					file>>trans[0]>>trans[1]>>trans[2];
					armatures.back()->scale_rest = XMFLOAT3(trans[0], trans[1], trans[2]);
					break;
				case 't':
					file>>trans[0]>>trans[1]>>trans[2];
					armatures.back()->translation_rest = XMFLOAT3(trans[0], trans[1], trans[2]);
					{
						XMMATRIX world = XMMatrixScalingFromVector(XMLoadFloat3(&armatures.back()->scale))*XMMatrixRotationQuaternion(XMLoadFloat4(&armatures.back()->rotation))*XMMatrixTranslationFromVector(XMLoadFloat3(&armatures.back()->translation));
						XMStoreFloat4x4( &armatures.back()->world_rest,world );
					}
					break;
				case 'b':
					{
						string boneName;
						file>>boneName;
						armatures.back()->boneCollection.push_back(new Bone(boneName));
						//stringstream ss("");
						//ss<<armatures.back()->name<<"_"<<boneName;
						//armatures.back()->boneCollection.push_back(new Bone(ss.str()));
						//transforms.insert(pair<string,Transform*>(armatures.back()->boneCollection.back()->name,armatures.back()->boneCollection.back()));
					}
					break;
				case 'p':
					file>>armatures.back()->boneCollection.back()->parentName;
					break;
				case 'l':
					{
						float x=0,y=0,z=0,w=0;
						file>>x>>y>>z>>w;
						XMVECTOR quaternion = XMVectorSet(x,y,z,w);
						file>>x>>y>>z;
						XMVECTOR translation = XMVectorSet(x,y,z,0);

						XMMATRIX frame;
						frame= XMMatrixRotationQuaternion(quaternion) * XMMatrixTranslationFromVector(translation) ;

						XMStoreFloat3(&armatures.back()->boneCollection.back()->translation_rest,translation);
						XMStoreFloat4(&armatures.back()->boneCollection.back()->rotation_rest,quaternion);
						XMStoreFloat4x4(&armatures.back()->boneCollection.back()->world_rest,frame);
						XMStoreFloat4x4(&armatures.back()->boneCollection.back()->restInv,XMMatrixInverse(0,frame));
						
						/*XMStoreFloat3( &armatures.back()->boneCollection.back().position,translationInverse );
						XMStoreFloat4( &armatures.back()->boneCollection.back().rotation,quaternionInverse );*/

						/*XMVECTOR sca,rot,tra;
						XMMatrixDecompose(&sca,&rot,&tra,XMMatrixInverse(0,XMMatrixTranspose(frame))*XMLoadFloat4x4(&armatures.back()->world));
						XMStoreFloat3( &armatures.back()->boneCollection.back().position,tra );
						XMStoreFloat4( &armatures.back()->boneCollection.back().rotation,rot );*/
						
					}
					break;
				case 'c':
					armatures.back()->boneCollection.back()->connected=true;
					break;
				case 'h':
					file>>armatures.back()->boneCollection.back()->length;
					break;
				default: break;
				}
			}
		}
	}
	file.close();



	//CREATE FAMILY
	for(Armature* armature : armatures){
		armature->CreateFamily();
	}

}
void RecursiveRest(Armature* armature, Bone* bone){
	Bone* parent = (Bone*)bone->parent;

	if(parent!=nullptr){
		XMMATRIX recRest = 
			XMLoadFloat4x4(&bone->world_rest)
			*
			XMLoadFloat4x4(&parent->recursiveRest)
			//*
			//XMLoadFloat4x4(&armature->boneCollection[boneI].rest)
			;
		XMStoreFloat4x4( &bone->recursiveRest, recRest );
		XMStoreFloat4x4( &bone->recursiveRestInv, XMMatrixInverse(0,recRest) );
	}
	else{
		bone->recursiveRest = bone->world_rest ;
		XMStoreFloat4x4( &bone->recursiveRestInv, XMMatrixInverse(0,XMLoadFloat4x4(&bone->recursiveRest)) );
	}

	for (unsigned int i = 0; i<bone->childrenI.size(); ++i){
		RecursiveRest(armature,bone->childrenI[i]);
	}
}
void LoadWiMaterialLibrary(const string& directory, const string& name, const string& identifier, const string& texturesDir,MaterialCollection& materials)
{
	int materialI=(int)(materials.size()-1);

	Material* currentMat = NULL;
	
	stringstream filename("");
	filename<<directory<<name;

	ifstream file(filename.str().c_str());
	if(file){
		while(!file.eof()){
			string line="";
			file>>line;
			if(line[0]=='/' && !strcmp(line.substr(2,8).c_str(),"MATERIAL")) {
				if (currentMat)
				{
					currentMat->ConvertToPhysicallyBasedMaterial();
					materials.insert(pair<string, Material*>(currentMat->name, currentMat));
				}
				
				stringstream identified_name("");
				identified_name<<line.substr(11,strlen(line.c_str())-11)<<identifier;
				currentMat = new Material(identified_name.str());
				materialI++;
			}
			else{
				switch(line[0]){
				case 'd':
					file>>currentMat->diffuseColor.x;
					file>>currentMat->diffuseColor.y;
					file>>currentMat->diffuseColor.z;
					break;
				case 'X':
					currentMat->cast_shadow=false;
					break;
				case 'r':
					{
						string resourceName="";
						file>>resourceName;
						stringstream ss("");
						ss<<directory<<texturesDir<<resourceName.c_str();
						currentMat->refMapName=ss.str();
						currentMat->refMap = (Texture2D*)wiResourceManager::GetGlobal()->add(ss.str());
					}
					break;
				case 'n':
					{
						string resourceName="";
						file>>resourceName;
						stringstream ss("");
						ss<<directory<<texturesDir<<resourceName.c_str();
						currentMat->normalMapName=ss.str();
						currentMat->normalMap = (Texture2D*)wiResourceManager::GetGlobal()->add(ss.str());
					}
					break;
				case 't':
					{
						string resourceName="";
						file>>resourceName;
						stringstream ss("");
						ss<<directory<<texturesDir<<resourceName.c_str();
						currentMat->textureName=ss.str();
						currentMat->texture = (Texture2D*)wiResourceManager::GetGlobal()->add(ss.str());
					}
					file>>currentMat->premultipliedTexture;
					break;
				case 'D':
					{
						string resourceName="";
						file>>resourceName;
						stringstream ss("");
						ss<<directory<<texturesDir<<resourceName.c_str();
						currentMat->displacementMapName=ss.str();
						currentMat->displacementMap = (Texture2D*)wiResourceManager::GetGlobal()->add(ss.str());
					}
					break;
				case 'S':
					{
						string resourceName="";
						file>>resourceName;
						stringstream ss("");
						ss<<directory<<texturesDir<<resourceName.c_str();
						currentMat->specularMapName=ss.str();
						currentMat->specularMap = (Texture2D*)wiResourceManager::GetGlobal()->add(ss.str());
					}
					break;
				case 'a':
					file>>currentMat->alpha;
					break;
				case 'h':
					currentMat->shadeless=true;
					break;
				case 'R':
					file>>currentMat->refractionIndex;
					break;
				case 'e':
					file>>currentMat->enviroReflection;
					break;
				case 's':
					file>>currentMat->specular.x;
					file>>currentMat->specular.y;
					file>>currentMat->specular.z;
					file>>currentMat->specular.w;
					break;
				case 'p':
					file>>currentMat->specular_power;
					break;
				case 'k':
					currentMat->isSky=true;
					break;
				case 'm':
					file>>currentMat->movingTex.x;
					file>>currentMat->movingTex.y;
					file>>currentMat->movingTex.z;
					currentMat->framesToWaitForTexCoordOffset=currentMat->movingTex.z;
					break;
				case 'w':
					currentMat->water=true;
					break;
				case 'u':
					currentMat->subsurfaceScattering=true;
					break;
				case 'b':
					{
						string blend;
						file>>blend;
						if(!blend.compare("ADD"))
							currentMat->blendFlag=BLENDMODE_ADDITIVE;
					}
					break;
				case 'i':
					{
						file>>currentMat->emissive;
					}
					break;
				default:break;
				}
			}
		}
	}
	file.close();
	
	if (currentMat)
	{
		currentMat->ConvertToPhysicallyBasedMaterial();
		materials.insert(pair<string, Material*>(currentMat->name, currentMat));
	}

}
void LoadWiObjects(const string& directory, const string& name, const string& identifier, list<Object*>& objects
					, list<Armature*>& armatures
				   , MeshCollection& meshes, const MaterialCollection& materials)
{
	
	stringstream filename("");
	filename<<directory<<name;

	ifstream file(filename.str().c_str());
	if(file){
		while(!file.eof()){
			float trans[] = { 0,0,0,0 };
			string line="";
			file>>line;
			if(line[0]=='/' && !strcmp(line.substr(2,6).c_str(),"OBJECT")) {
				stringstream identified_name("");
				identified_name<<line.substr(9,strlen(line.c_str())-9)<<identifier;
				objects.push_back(new Object(identified_name.str()));
			}
			else{
				switch(line[0]){
				case 'm':
					{
						string meshName="";
						file>>meshName;
						stringstream identified_mesh("");
						identified_mesh<<meshName<<identifier;
						objects.back()->meshfile = identified_mesh.str();
						MeshCollection::iterator iter = meshes.find(identified_mesh.str());
						
						if(line[1]=='b'){ //binary load mesh in place if not present

							if(iter==meshes.end()){
								stringstream meshFileName("");
								meshFileName<<directory<<meshName<<".wimesh";
								Mesh* mesh = new Mesh();
								mesh->LoadFromFile(identified_mesh.str(),meshFileName.str(),materials,armatures,identifier);
								objects.back()->mesh=mesh;
								meshes.insert(pair<string,Mesh*>(identified_mesh.str(),mesh));
							}
							else{
								objects.back()->mesh=iter->second;
							}
						}
						else{
							if(iter!=meshes.end()) {
								objects.back()->mesh=iter->second;
								//objects.back()->mesh->usedBy.push_back(objects.size()-1);
							}
						}
					}
					break;
				case 'p':
					{
						string parentName="";
						file>>parentName;
						stringstream identified_parentName("");
						identified_parentName<<parentName<<identifier;
						objects.back()->parentName = identified_parentName.str();
						//for(Armature* a : armatures){
						//	if(!a->name.compare(identified_parentName.str())){
						//		objects.back()->parentName=identified_parentName.str();
						//		objects.back()->parent=a;
						//		objects.back()->attachTo(a,1,1,1);
						//		objects.back()->armatureDeform=true;
						//	}
						//}
					}
					break;
				case 'b':
					{
						string bone="";
						file>>bone;
						objects.back()->boneParent = bone;
						//if(objects.back()->parent!=nullptr){
						//	for(Bone* b : ((Armature*)objects.back()->parent)->boneCollection){
						//		if(!bone.compare(b->name)){
						//			objects.back()->parent=b;
						//			objects.back()->armatureDeform=false;
						//			break;
						//		}
						//	}
						//}
					}
					break;
				case 'I':
					{
						XMFLOAT3 s,t;
						XMFLOAT4 r;
						file>>t.x>>t.y>>t.z>>r.x>>r.y>>r.z>>r.w>>s.x>>s.y>>s.z;
						XMStoreFloat4x4(&objects.back()->parent_inv_rest
								, XMMatrixScalingFromVector(XMLoadFloat3(&s)) *
									XMMatrixRotationQuaternion(XMLoadFloat4(&r)) *
									XMMatrixTranslationFromVector(XMLoadFloat3(&t))
							);
					}
					break;
				case 'r':
					file>>trans[0]>>trans[1]>>trans[2]>>trans[3];
					objects.back()->Rotate(XMFLOAT4(trans[0], trans[1], trans[2],trans[3]));
					//objects.back()->rotation_rest = XMFLOAT4(trans[0],trans[1],trans[2],trans[3]);
					break;
				case 's':
					file>>trans[0]>>trans[1]>>trans[2];
					objects.back()->Scale(XMFLOAT3(trans[0], trans[1], trans[2]));
					//objects.back()->scale_rest = XMFLOAT3(trans[0], trans[1], trans[2]);
					break;
				case 't':
					file>>trans[0]>>trans[1]>>trans[2];
					objects.back()->Translate(XMFLOAT3(trans[0], trans[1], trans[2]));
					//objects.back()->translation_rest = XMFLOAT3(trans[0], trans[1], trans[2]);
					//XMStoreFloat4x4( &objects.back()->world_rest, XMMatrixScalingFromVector(XMLoadFloat3(&objects.back()->scale_rest))
					//											*XMMatrixRotationQuaternion(XMLoadFloat4(&objects.back()->rotation_rest))
					//											*XMMatrixTranslationFromVector(XMLoadFloat3(&objects.back()->translation_rest))
					//										);
					//objects.back()->world=objects.back()->world_rest;
					break;
				case 'E':
					{
						string systemName,materialName;
						bool visibleEmitter;
						float size,randfac,norfac;
						float count,life,randlife;
						float scaleX,scaleY,rot;
						file>>systemName>>visibleEmitter>>materialName>>size>>randfac>>norfac>>count>>life>>randlife;
						file>>scaleX>>scaleY>>rot;
						
						if(visibleEmitter) objects.back()->emitterType=Object::EMITTER_VISIBLE;
						else if(objects.back()->emitterType ==Object::NO_EMITTER) objects.back()->emitterType =Object::EMITTER_INVISIBLE;

						if(wiRenderer::EMITTERSENABLED){
							stringstream identified_materialName("");
							identified_materialName<<materialName<<identifier;
							stringstream identified_systemName("");
							identified_systemName<<systemName<<identifier;
							if(objects.back()->mesh){
								objects.back()->eParticleSystems.push_back( 
									new wiEmittedParticle(identified_systemName.str(),identified_materialName.str(),objects.back(),size,randfac,norfac,count,life,randlife,scaleX,scaleY,rot) 
									);
							}
						}
					}
					break;
				case 'H':
					{
						string name,mat,densityG,lenG;
						float len;
						int count;
						file>>name>>mat>>len>>count>>densityG>>lenG;
						
						if(wiRenderer::HAIRPARTICLEENABLED){
							stringstream identified_materialName("");
							identified_materialName<<mat<<identifier;
							objects.back()->hParticleSystems.push_back(new wiHairParticle(name,len,count,identified_materialName.str(),objects.back(),densityG,lenG) );
						}
					}
					break;
				case 'P':
					objects.back()->rigidBody = true;
					file>>objects.back()->collisionShape>>objects.back()->mass>>
						objects.back()->friction>>objects.back()->restitution>>objects.back()->damping>>objects.back()->physicsType>>
						objects.back()->kinematic;
					break;
				case 'T':
					file >> objects.back()->transparency;
					break;
				default: break;
				}
			}
		}
	}
	file.close();

	//for (unsigned int i = 0; i<objects.size(); i++){
	//	if(objects[i]->mesh){
	//		if(objects[i]->mesh->trailInfo.base>=0 && objects[i]->mesh->trailInfo.tip>=0){
	//			//objects[i]->trail.resize(MAX_RIBBONTRAILS);
	//			GPUBufferDesc bd;
	//			ZeroMemory( &bd, sizeof(bd) );
	//			bd.Usage = USAGE_DYNAMIC;
	//			bd.ByteWidth = sizeof( RibbonVertex ) * 1000;
	//			bd.BindFlags = BIND_VERTEX_BUFFER;
	//			bd.CPUAccessFlags = CPU_ACCESS_WRITE;
	//			wiRenderer::GetDevice()->CreateBuffer( &bd, NULL, &objects[i]->trailBuff );
	//			objects[i]->trailTex = wiTextureHelper::getInstance()->getTransparent();
	//			objects[i]->trailDistortTex = wiTextureHelper::getInstance()->getNormalMapDefault();
	//		}
	//	}
	//}

	//for (MeshCollection::iterator iter = meshes.begin(); iter != meshes.end(); ++iter){
	//	Mesh* iMesh = iter->second;

	//	iMesh->CreateVertexArrays();
	//	iMesh->Optimize();
	//	iMesh->CreateBuffers();
	//}

}
void LoadWiMeshes(const string& directory, const string& name, const string& identifier, MeshCollection& meshes, 
	const list<Armature*>& armatures, const MaterialCollection& materials)
{
	int meshI=(int)(meshes.size()-1);
	Mesh* currentMesh = NULL;
	
	stringstream filename("");
	filename<<directory<<name;

	ifstream file(filename.str().c_str());
	if(file){
		while(!file.eof()){
			float trans[] = { 0,0,0,0 };
			string line="";
			file>>line;
			if(line[0]=='/' && !line.substr(2,4).compare("MESH")) {
				stringstream identified_name("");
				identified_name<<line.substr(7,strlen(line.c_str())-7)<<identifier;
				currentMesh = new Mesh(identified_name.str());
				meshes.insert( pair<string,Mesh*>(currentMesh->name,currentMesh) );
				meshI++;
			}
			else{
				switch(line[0]){
				case 'p':
					{
						string parentArmature="";
						file>>parentArmature;

						stringstream identified_parentArmature("");
						identified_parentArmature<<parentArmature<<identifier;
						currentMesh->parent=identified_parentArmature.str();
						//for (unsigned int i = 0; i<armatures.size(); ++i)
						//	if(!strcmp(armatures[i]->name.c_str(),currentMesh->parent.c_str())){
						//		currentMesh->armature=armatures[i];
						//	}
						for (auto& a : armatures)
						{
							if (!a->name.compare(currentMesh->parent))
							{
								currentMesh->armature = a;
								break;
							}
						}
					}
					break;
				case 'v': 
					currentMesh->vertices.push_back(SkinnedVertex());
					file >> currentMesh->vertices.back().pos.x;
					file >> currentMesh->vertices.back().pos.y;
					file >> currentMesh->vertices.back().pos.z;
					break;
				case 'n':
					if (currentMesh->isBillboarded){
						currentMesh->vertices.back().nor.x = currentMesh->billboardAxis.x;
						currentMesh->vertices.back().nor.y = currentMesh->billboardAxis.y;
						currentMesh->vertices.back().nor.z = currentMesh->billboardAxis.z;
					}
					else{
						file >> currentMesh->vertices.back().nor.x;
						file >> currentMesh->vertices.back().nor.y;
						file >> currentMesh->vertices.back().nor.z;
					}
					break;
				case 'u':
					file >> currentMesh->vertices.back().tex.x;
					file >> currentMesh->vertices.back().tex.y;
					//texCoordFill++;
					break;
				case 'w':
					{
						string nameB;
						float weight=0;
						int BONEINDEX=0;
						file>>nameB>>weight;
						//bool gotArmature=false;
						bool gotBone=false;
						//int i=0;
						//while(!gotArmature && i<(int)armatures.size()){  //SEARCH FOR PARENT ARMATURE
						//	if(!strcmp(armatures[i]->name.c_str(),currentMesh->parent.c_str()))
						//		gotArmature=true;
						//	else i++;
						//}
						if(currentMesh->armature != nullptr){
							int j=0;
							//while(!gotBone && j<(int)currentMesh->armature->boneCollection.size()){
							//	if(!armatures[i]->boneCollection[j]->name.compare(nameB)){
							//		gotBone=true;
							//		BONEINDEX=j; //GOT INDEX OF BONE OF THE WEIGHT IN THE PARENT ARMATURE
							//	}
							//	j++;
							//}
							for (auto& b : currentMesh->armature->boneCollection)
							{
								if (!b->name.compare(nameB))
								{
									BONEINDEX = j;
									break;
								}
								j++;
							}
						}
						if(gotBone){ //ONLY PROCEED IF CORRESPONDING BONE WAS FOUND
							if(!currentMesh->vertices.back().wei.x) {
								currentMesh->vertices.back().wei.x=weight;
								currentMesh->vertices.back().bon.x=(float)BONEINDEX;
							}
							else if(!currentMesh->vertices.back().wei.y) {
								currentMesh->vertices.back().wei.y=weight;
								currentMesh->vertices.back().bon.y=(float)BONEINDEX;
							}
							else if(!currentMesh->vertices.back().wei.z) {
								currentMesh->vertices.back().wei.z=weight;
								currentMesh->vertices.back().bon.z=(float)BONEINDEX;
							}
							else if(!currentMesh->vertices.back().wei.w) {
								currentMesh->vertices.back().wei.w=weight;
								currentMesh->vertices.back().bon.w=(float)BONEINDEX;
							}
						}

						 //(+RIBBONTRAIL SETUP)(+VERTEXGROUP SETUP)

						if(nameB.find("trailbase")!=string::npos)
							currentMesh->trailInfo.base = (int)(currentMesh->vertices.size()-1);
						else if(nameB.find("trailtip")!=string::npos)
							currentMesh->trailInfo.tip = (int)(currentMesh->vertices.size()-1);
						
						bool windAffection=false;
						if(nameB.find("wind")!=string::npos)
							windAffection=true;
						bool gotvg=false;
						for (unsigned int v = 0; v<currentMesh->vertexGroups.size(); ++v)
							if(!nameB.compare(currentMesh->vertexGroups[v].name)){
								gotvg=true;
								currentMesh->vertexGroups[v].addVertex(VertexRef((int)(currentMesh->vertices.size() - 1), weight));
								if(windAffection)
									currentMesh->vertices.back().tex.w=weight;
							}
						if(!gotvg){
							currentMesh->vertexGroups.push_back(VertexGroup(nameB));
							currentMesh->vertexGroups.back().addVertex(VertexRef((int)(currentMesh->vertices.size() - 1), weight));
							if(windAffection)
								currentMesh->vertices.back().tex.w=weight;
						}
					}
					break;
				case 'i':
					{
						int count;
						file>>count;
						for(int i=0;i<count;i++){
							int index;
							file>>index;
							currentMesh->indices.push_back(index);
						}
						break;
					}
				case 'V': 
					{
						XMFLOAT3 pos;
						file >> pos.x>>pos.y>>pos.z;
						currentMesh->physicsverts.push_back(pos);
					}
					break;
				case 'I':
					{
						int count;
						file>>count;
						for(int i=0;i<count;i++){
							int index;
							file>>index;
							currentMesh->physicsindices.push_back(index);
						}
						break;
					}
				case 'm':
					{
						string mName="";
						file>>mName;
						stringstream identified_material("");
						identified_material<<mName<<identifier;
						currentMesh->materialNames.push_back(identified_material.str());
						MaterialCollection::const_iterator iter = materials.find(identified_material.str());
						if(iter!=materials.end()) {
							currentMesh->subsets.push_back(MeshSubset());
							currentMesh->renderable=true;
							currentMesh->subsets.back().material = (iter->second);
							//currentMesh->materialIndices.push_back(currentMesh->materials.size()); //CONNECT meshes WITH MATERIALS
						}
					}
					break;
				case 'a':
					file>>currentMesh->vertices.back().tex.z;
					break;
				case 'B':
					for(int corner=0;corner<8;++corner){
						file>>currentMesh->aabb.corners[corner].x;
						file>>currentMesh->aabb.corners[corner].y;
						file>>currentMesh->aabb.corners[corner].z;
					}
					break;
				case 'b':
					{
						currentMesh->isBillboarded=true;
						string read = "";
						file>>read;
						transform(read.begin(), read.end(), read.begin(), toupper);
						if(read.find(toupper('y'))!=string::npos) currentMesh->billboardAxis=XMFLOAT3(0,1,0);
						else if(read.find(toupper('x'))!=string::npos) currentMesh->billboardAxis=XMFLOAT3(1,0,0);
						else if(read.find(toupper('z'))!=string::npos) currentMesh->billboardAxis=XMFLOAT3(0,0,1);
						else currentMesh->billboardAxis=XMFLOAT3(0,0,0);
					}
					break;
				case 'S':
					{
						currentMesh->softBody=true;
						string mvgi="",gvgi="",svgi="";
						file>>currentMesh->mass>>currentMesh->friction>>gvgi>>mvgi>>svgi;
						for (unsigned int v = 0; v<currentMesh->vertexGroups.size(); ++v){
							if(!strcmp(mvgi.c_str(),currentMesh->vertexGroups[v].name.c_str()))
								currentMesh->massVG=v;
							if(!strcmp(gvgi.c_str(),currentMesh->vertexGroups[v].name.c_str()))
								currentMesh->goalVG=v;
							if(!strcmp(svgi.c_str(),currentMesh->vertexGroups[v].name.c_str()))
								currentMesh->softVG=v;
						}
					}
					break;
				default: break;
				}
			}
		}
	}
	file.close();
	
	if(currentMesh)
		meshes.insert( pair<string,Mesh*>(currentMesh->name,currentMesh) );

}
void LoadWiActions(const string& directory, const string& name, const string& identifier, list<Armature*>& armatures)
{
	Armature* armatureI=nullptr;
	Bone* boneI=nullptr;
	int firstFrame=INT_MAX;

	stringstream filename("");
	filename<<directory<<name;

	ifstream file(filename.str().c_str());
	if(file){
		while(!file.eof()){
			string line="";
			file>>line;
			if(line[0]=='/' && !strcmp(line.substr(2,8).c_str(),"ARMATURE")) {
				stringstream identified_name("");
				identified_name<<line.substr(11,strlen(line.c_str())-11)<<identifier;
				string armaturename = identified_name.str() ;
				//for (unsigned int i = 0; i<armatures.size(); i++)
				//	if(!armatures[i]->name.compare(armaturename)){
				//		armatureI=i;
				//		break;
				//	}
				for (auto& a : armatures)
				{
					if (!a->name.compare(armaturename)) {
						armatureI = a;
						break;
					}
				}
			}
			else{
				switch(line[0]){
				case 'C':
					armatureI->actions.push_back(Action());
					file>> armatureI->actions.back().name;
					break;
				case 'A':
					file>> armatureI->actions.back().frameCount;
					break;
				case 'b':
					{
						string boneName;
						file>>boneName;
						//for (unsigned int i = 0; i<armatures[armatureI]->boneCollection.size(); i++)
						//	if(!armatures[armatureI]->boneCollection[i]->name.compare(boneName)){
						//		boneI=i;
						//		break;
						//	} //GOT BONE INDEX
						//armatures[armatureI]->boneCollection[boneI]->actionFrames.resize(armatures[armatureI]->actions.size());
						boneI = armatureI->GetBone(boneName);
						if (boneI != nullptr)
						{
							boneI->actionFrames.resize(armatureI->actions.size());
						}
					}
					break;
				case 'r':
					{
						int f = 0;
						float x=0,y=0,z=0,w=0;
						file>>f>>x>>y>>z>>w;
						if (boneI != nullptr)
							boneI->actionFrames.back().keyframesRot.push_back(KeyFrame(f,x,y,z,w));
					}
					break;
				case 't':
					{
						int f = 0;
						float x=0,y=0,z=0;
						file>>f>>x>>y>>z;
						if (boneI != nullptr)
							boneI->actionFrames.back().keyframesPos.push_back(KeyFrame(f,x,y,z,0));
					}
					break;
				case 's':
					{
						int f = 0;
						float x=0,y=0,z=0;
						file>>f>>x>>y>>z;
						if(boneI!=nullptr)
							boneI->actionFrames.back().keyframesSca.push_back(KeyFrame(f,x,y,z,0));
					}
					break;
				default: break;
				}
			}
		}
	}
	file.close();
}
void LoadWiLights(const string& directory, const string& name, const string& identifier, list<Light*>& lights)
{

	stringstream filename("");
	filename<<directory<<name;

	ifstream file(filename.str().c_str());
	if(file){
		while(!file.eof()){
			string line="";
			file>>line;
			switch(line[0]){
			case 'P':
				{
					lights.push_back(new Light());
					lights.back()->type=Light::POINT;
					string lname = "";
					file>>lname>> lights.back()->shadow;
					stringstream identified_name("");
					identified_name<<lname<<identifier;
					lights.back()->name=identified_name.str();
					lights.back()->shadowBias = 0.00001f;
					
				}
				break;
			case 'D':
				{
					lights.push_back(new Light());
					lights.back()->type=Light::DIRECTIONAL;
					file>>lights.back()->name; 
					lights.back()->shadow = true;
					//lights.back()->shadowMaps_dirLight.resize(3);
					lights.back()->shadowBias = 9.99995464e-005f;
					//for (int i = 0; i < 3; ++i)
					//{
					//	lights.back()->shadowMaps_dirLight[i].Initialize(
					//		wiRenderer::SHADOWMAPRES, wiRenderer::SHADOWMAPRES
					//		, 0, true
					//		);
					//}
				}
				break;
			case 'S':
				{
					lights.push_back(new Light());
					lights.back()->type=Light::SPOT;
					file>>lights.back()->name;
					file>>lights.back()->shadow>>lights.back()->enerDis.z;
					lights.back()->shadowBias = 0.00001f;
				}
				break;
			case 'p':
				{
					string parentName="";
					file>>parentName;
					
					stringstream identified_parentName("");
					identified_parentName<<parentName<<identifier;
					lights.back()->parentName=identified_parentName.str();
					//for(map<string,Transform*>::iterator it=transforms.begin();it!=transforms.end();++it){
					//	if(!it->second->name.compare(lights.back()->parentName)){
					//		lights.back()->parent=it->second;
					//		lights.back()->attachTo(it->second,1,1,1);
					//		break;
					//	}
					//}
				}
				break;
			case 'b':
				{
					string parentBone="";
					file>>parentBone;
					lights.back()->boneParent = parentBone;

					//for(Bone* b : ((Armature*)lights.back()->parent)->boneCollection){
					//	if(!b->name.compare(parentBone)){
					//		lights.back()->parent=b;
					//		lights.back()->attachTo(b,1,1,1);
					//	}
					//}
				}
				break;
			case 'I':
				{
					XMFLOAT3 s,t;
					XMFLOAT4 r;
					file>>t.x>>t.y>>t.z>>r.x>>r.y>>r.z>>r.w>>s.x>>s.y>>s.z;
					XMStoreFloat4x4(&lights.back()->parent_inv_rest
							, XMMatrixScalingFromVector(XMLoadFloat3(&s)) *
								XMMatrixRotationQuaternion(XMLoadFloat4(&r)) *
								XMMatrixTranslationFromVector(XMLoadFloat3(&t))
						);
				}
				break;
			case 't':
				{
					float x,y,z;
					file>>x>>y>>z;
					lights.back()->Translate(XMFLOAT3(x, y, z));
					//lights.back()->translation_rest=XMFLOAT3(x,y,z);
					break;
				}
			case 'r':
				{
					float x,y,z,w;
					file>>x>>y>>z>>w;
					lights.back()->Rotate(XMFLOAT4(x, y, z, w));
					//lights.back()->rotation_rest=XMFLOAT4(x,y,z,w);
					break;
				}
			case 'c':
				{
					float r,g,b;
					file>>r>>g>>b;
					lights.back()->color=XMFLOAT4(r,g,b,0);
					break;
				}
			case 'e':
				file>>lights.back()->enerDis.x;
				break;
			case 'd':
				file>>lights.back()->enerDis.y;
				//lights.back()->enerDis.y *= XMVectorGetX( world.r[0] )*0.1f;
				break;
			case 'n':
				lights.back()->noHalo=true;
				break;
			case 'l':
				{
					string t="";
					file>>t;
					stringstream rim("");
					rim<<directory<<"rims/"<<t;
					Texture2D* tex=nullptr;
					if ((tex = (Texture2D*)wiResourceManager::GetGlobal()->add(rim.str())) != nullptr){
						lights.back()->lensFlareRimTextures.push_back(tex);
						lights.back()->lensFlareNames.push_back(rim.str());
					}
				}
				break;
			default: break;
			}
		}

		//for(MeshCollection::iterator iter=lightGwiRenderer.begin(); iter!=lightGwiRenderer.end(); ++iter){
		//	Mesh* iMesh = iter->second;
		//	GPUBufferDesc bd;
		//	ZeroMemory( &bd, sizeof(bd) );
		//	bd.Usage = USAGE_DYNAMIC;
		//	bd.ByteWidth = sizeof( Instance )*iMesh->usedBy.size();
		//	bd.BindFlags = BIND_VERTEX_BUFFER;
		//	bd.CPUAccessFlags = CPU_ACCESS_WRITE;
		//	wiRenderer::GetDevice()->CreateBuffer( &bd, 0, &iMesh->meshInstanceBuffer );
		//}
	}
	file.close();
}
void LoadWiHitSpheres(const string& directory, const string& name, const string& identifier, vector<HitSphere*>& spheres
					  ,const list<Armature*>& armatures)
{
	stringstream filename("");
	filename<<directory<<name;

	ifstream file(filename.str().c_str());
	if(file)
	{
		string voidStr="";
		file>>voidStr;
		while(!file.eof()){
			string line="";
			file>>line;
			switch(line[0]){
			case 'H':
				{
					string name;
					float scale;
					XMFLOAT3 loc;
					string parentStr;
					string prop;
					file>>name>>scale>>loc.x>>loc.y>>loc.z>>parentStr>>prop;
			
					stringstream identified_parent(""),identified_name("");
					identified_parent<<parentStr<<identifier;
					identified_name<<name<<identifier;
					//Armature* parentA=nullptr;
					//Transform* parent=nullptr;
					//for(Armature* a:armatures){
					//	if(parentArmature.compare(a->name)){
					//		for(Bone* b:a->boneCollection){
					//			if(!parentBone.compare(b->name)){
					//				parentA=a;
					//				parent=b;
					//			}
					//		}
					//	}
					//}
					//spheres.push_back(new HitSphere(identified_name.str(),scale,loc,parentA,parent,prop));

					// PARENTING DISABLED ON REFACTOR! CHECK!
					//Transform* parent = nullptr;
					//if(transforms.find(identified_parent.str())!=transforms.end())
					//{
					//	parent = transforms[identified_parent.str()];
					//	spheres.push_back(new HitSphere(identified_name.str(),scale,loc,parent,prop));
					//	spheres.back()->attachTo(parent,1,1,1);
					//	transforms.insert(pair<string,Transform*>(spheres.back()->name,spheres.back()));
					//}

				}
				break;
			case 'I':
				{
					XMFLOAT3 s,t;
					XMFLOAT4 r;
					file>>t.x>>t.y>>t.z>>r.x>>r.y>>r.z>>r.w>>s.x>>s.y>>s.z;
					XMStoreFloat4x4(&spheres.back()->parent_inv_rest
							, XMMatrixScalingFromVector(XMLoadFloat3(&s)) *
								XMMatrixRotationQuaternion(XMLoadFloat4(&r)) *
								XMMatrixTranslationFromVector(XMLoadFloat3(&t))
						);
				}
				break;
			case 'b':
				{
					string parentBone = "";
					file>>parentBone;
					Armature* parentA = (Armature*)spheres.back()->parent;
					if(parentA!=nullptr){
						for(Bone* b:parentA->boneCollection){
							if(!parentBone.compare(b->name)){
								spheres.back()->attachTo(b,1,1,1);
							}
						}
					}
				}
				break;
			default: break;
			};
		}
	}
	file.close();


	////SET UP SPHERE INDEXERS
	//for(int i=0;i<spheres.size();i++){
	//	for(int j=0;j<armatures.size();j++){
	//		if(!armatures[j]->name.compare(spheres[i]->pA)){
	//			spheres[i]->parentArmature=armatures[j];
	//			for(int k=0;k<armatures[j]->boneCollection.size();k++)
	//				if(!armatures[j]->boneCollection[k]->name.compare(spheres[i]->pB)){
	//					spheres[i]->parentBone=k;
	//					break;
	//				}
	//			break;
	//		}
	//	}
	//}
}
void LoadWiWorldInfo(const string&directory, const string& name, WorldInfo& worldInfo, Wind& wind){
	stringstream filename("");
	filename<<directory<<name;

	ifstream file(filename.str().c_str());
	if(file){
		while(!file.eof()){
			string read = "";
			file>>read;
			switch(read[0]){
			case 'h':
				file>>worldInfo.horizon.x>>worldInfo.horizon.y>>worldInfo.horizon.z;
				break;
			case 'z':
				file>>worldInfo.zenith.x>>worldInfo.zenith.y>>worldInfo.zenith.z;
				break;
			case 'a':
				file>>worldInfo.ambient.x>>worldInfo.ambient.y>>worldInfo.ambient.z;
				break;
			case 'W':
				{
					XMFLOAT4 r;
					float s;
					file>>r.x>>r.y>>r.z>>r.w>>s;
					XMStoreFloat3(&wind.direction, XMVector3Transform( XMVectorSet(0,s,0,0),XMMatrixRotationQuaternion(XMLoadFloat4(&r)) ));
				}
				break;
			case 'm':
				{
					float s,e,h;
					file>>s>>e>>h;
					worldInfo.fogSEH=XMFLOAT3(s,e,h);
				}
				break;
			default:break;
			}
		}
	}
	file.close();
}
void LoadWiCameras(const string&directory, const string& name, const string& identifier, vector<Camera>& cameras
				   ,const list<Armature*>& armatures){
	stringstream filename("");
	filename<<directory<<name;

	ifstream file(filename.str().c_str());
	if(file)
	{
		string voidStr("");
		file>>voidStr;
		while(!file.eof()){
			string line="";
			file>>line;
			switch(line[0]){

			case 'c':
				{
					XMFLOAT3 trans;
					XMFLOAT4 rot;
					string name(""),parentA(""),parentB("");
					file>>name>>parentA>>parentB>>trans.x>>trans.y>>trans.z>>rot.x>>rot.y>>rot.z>>rot.w;

			
					stringstream identified_parentArmature("");
					identified_parentArmature<<parentA<<identifier;
			
					cameras.push_back(Camera(
						trans,rot
						,name)
						);

					//for (unsigned int i = 0; i<armatures.size(); ++i){
					//	if(!armatures[i]->name.compare(identified_parentArmature.str())){
					//		for (unsigned int j = 0; j<armatures[i]->boneCollection.size(); ++j){
					//			if(!armatures[i]->boneCollection[j]->name.compare(parentB.c_str()))
					//				cameras.back().attachTo(armatures[i]->boneCollection[j]);
					//		}
					//	}
					//}
					for (auto& a : armatures)
					{
						Bone* b = a->GetBone(parentB);
						if (b != nullptr)
						{
							cameras.back().attachTo(b);
						}
					}

				}
				break;
			case 'I':
				{
					XMFLOAT3 s,t;
					XMFLOAT4 r;
					file>>t.x>>t.y>>t.z>>r.x>>r.y>>r.z>>r.w>>s.x>>s.y>>s.z;
					XMStoreFloat4x4(&cameras.back().parent_inv_rest
							, XMMatrixScalingFromVector(XMLoadFloat3(&s)) *
								XMMatrixRotationQuaternion(XMLoadFloat4(&r)) *
								XMMatrixTranslationFromVector(XMLoadFloat3(&t))
						);
				}
				break;
			default:break;
			}
		}
	}
	file.close();
}
void LoadWiDecals(const string&directory, const string& name, const string& texturesDir, list<Decal*>& decals){
	stringstream filename("");
	filename<<directory<<name;

	ifstream file(filename.str().c_str());
	if(file)
	{
		string voidStr="";
		file>>voidStr;
		while(!file.eof()){
			string line="";
			file>>line;
			switch(line[0]){
			case 'd':
				{
					string name;
					XMFLOAT3 loc,scale;
					XMFLOAT4 rot;
					file>>name>>scale.x>>scale.y>>scale.z>>loc.x>>loc.y>>loc.z>>rot.x>>rot.y>>rot.z>>rot.w;
					Decal* decal = new Decal();
					decal->name=name;
					decal->translation_rest=loc;
					decal->scale_rest=scale;
					decal->rotation_rest=rot;
					decals.push_back(new Decal(loc,scale,rot));
				}
				break;
			case 't':
				{
					string tex="";
					file>>tex;
					stringstream ss("");
					ss<<directory<<texturesDir<<tex;
					decals.back()->addTexture(ss.str());
				}
				break;
			case 'n':
				{
					string tex="";
					file>>tex;
					stringstream ss("");
					ss<<directory<<texturesDir<<tex;
					decals.back()->addNormal(ss.str());
				}
				break;
			default:break;
			};
		}
	}
	file.close();
}


//void LoadFromDisk(const string& dir, const string& name, const string& identifier
//				  , vector<Armature*>& armatures
//				  , MaterialCollection& materials
//				  , vector<Object*>& objects
//				  , MeshCollection& meshes
//				  , vector<Light*>& lights
//				  , vector<HitSphere*>& spheres
//				  , WorldInfo& worldInfo, Wind& wind
//				  , vector<Camera>& cameras
//				  , map<string,Transform*>& transforms
//				  , list<Decal*>& decals
//				  )
//{
//	MaterialCollection		l_materials;
//	vector<Armature*>		l_armatures;
//	vector<Object*>			l_objects;
//	MeshCollection			l_meshes;
//	vector<Light*>			l_lights;
//	vector<HitSphere*>		l_spheres;
//	WorldInfo				l_worldInfo = worldInfo;
//	Wind					l_wind = wind;
//	vector<Camera>			l_cameras;
//	map<string,Transform*>  l_transforms;
//	list<Decal*>			l_decals;
//
//	stringstream directory(""),armatureFilePath(""),materialLibFilePath(""),meshesFilePath(""),objectsFilePath("")
//		,actionsFilePath(""),lightsFilePath(""),worldInfoFilePath(""),enviroMapFilePath(""),hitSpheresFilePath("")
//		,camerasFilePath(""),decalsFilePath("");
//
//	directory<<dir;
//	armatureFilePath<<name<<".wia";
//	materialLibFilePath<<name<<".wim";
//	meshesFilePath<<name<<".wi";
//	objectsFilePath<<name<<".wio";
//	actionsFilePath<<name<<".wiact";
//	lightsFilePath<<name<<".wil";
//	worldInfoFilePath<<name<<".wiw";
//	hitSpheresFilePath<<name<<".wih";
//	camerasFilePath<<name<<".wic";
//	decalsFilePath<<name<<".wid";
//
//	LoadWiArmatures(directory.str(), armatureFilePath.str(),identifier,l_armatures,l_transforms);
//	LoadWiMaterialLibrary(directory.str(), materialLibFilePath.str(),identifier, "textures/", l_materials);
//	LoadWiMeshes(directory.str(), meshesFilePath.str(),identifier,meshes,l_armatures,l_materials);
//	LoadWiObjects(directory.str(), objectsFilePath.str(),identifier,l_objects,l_armatures,meshes,l_transforms,l_materials);
//	LoadWiActions(directory.str(), actionsFilePath.str(),identifier,l_armatures);
//	LoadWiLights(directory.str(), lightsFilePath.str(),identifier, l_lights, l_armatures,l_transforms);
//	LoadWiHitSpheres(directory.str(), hitSpheresFilePath.str(),identifier,spheres,l_armatures,l_transforms);
//	LoadWiCameras(directory.str(), camerasFilePath.str(),identifier,l_cameras,l_armatures,l_transforms);
//	LoadWiWorldInfo(directory.str(), worldInfoFilePath.str(),l_worldInfo,l_wind);
//	LoadWiDecals(directory.str(), decalsFilePath.str(), "textures/", l_decals);
//
//	wiRenderer::graphicsMutex.lock();
//	{
//		armatures.insert(armatures.end(),l_armatures.begin(),l_armatures.end());
//		objects.insert(objects.end(),l_objects.begin(),l_objects.end());
//		lights.insert(lights.end(),l_lights.begin(),l_lights.end());
//		spheres.insert(spheres.end(),l_spheres.begin(),l_spheres.end());
//		cameras.insert(cameras.end(),l_cameras.begin(),l_cameras.end());
//
//		meshes.insert(l_meshes.begin(),l_meshes.end());
//		materials.insert(l_materials.begin(),l_materials.end());
//
//		worldInfo=l_worldInfo;
//		wind=l_wind;
//
//		transforms.insert(l_transforms.begin(),l_transforms.end());
//
//		decals.insert(decals.end(),l_decals.begin(),l_decals.end());
//	}
//	wiRenderer::graphicsMutex.unlock();
//}


void GenerateSPTree(wiSPTree*& tree, vector<Cullable*>& objects, int type){
	if(type==SPTREE_GENERATE_QUADTREE)
		tree = new QuadTree();
	else if(type==SPTREE_GENERATE_OCTREE)
		tree = new Octree();
	tree->initialize(objects);
}

#pragma region SKINNEDVERTEX
void SkinnedVertex::Serialize(wiArchive& archive)
{
	if (archive.IsReadMode())
	{
		archive >> pos;
		archive >> nor;
		archive >> tex;
		archive >> bon;
		archive >> wei;
	}
	else
	{
		archive << pos;
		archive << nor;
		archive << tex;
		archive << bon;
		archive << wei;
	}
}
#pragma endregion

#pragma region SCENE
Scene::Scene()
{
	models.push_back(new Model);
	models.back()->name = "[WickedEngine-default]{WorldNode}";
}
Scene::~Scene()
{
	for (Model* x : models)
	{
		SAFE_DELETE(x);
	}
}
void Scene::ClearWorld()
{
	Model* world = GetWorldNode();
	int i = 0;
	for (auto& x : models)
	{
		if (i == 0)
			continue;
		SAFE_DELETE(x);
		i++;
	}
	models.clear();
	models.push_back(world);

	for (auto& x : environmentProbes)
	{
		SAFE_DELETE(x);
	}
	environmentProbes.clear();
}
Model* Scene::GetWorldNode()
{
	return models[0];
}
void Scene::AddModel(Model* model)
{
	models.push_back(model);
	model->attachTo(models[0]);
}
void Scene::Update()
{
	models[0]->UpdateTransform();

	for (Model* x : models)
	{
		x->UpdateModel();
	}
}
#pragma endregion

#pragma region CULLABLE
Cullable::Cullable():bounds(AABB())/*,lastSquaredDistMulThousand(0)*/{}
void Cullable::Serialize(wiArchive& archive)
{
	bounds.Serialize(archive);
}
#pragma endregion

#pragma region STREAMABLE
Streamable::Streamable():directory(""),meshfile(""),materialfile(""),loaded(false){}
void Streamable::StreamIn()
{
}
void Streamable::StreamOut()
{
}
void Streamable::Serialize(wiArchive& archive)
{
	Cullable::Serialize(archive);

	if (archive.IsReadMode())
	{
		archive >> directory;
		archive >> meshfile;
		archive >> materialfile;
		archive >> loaded;
	}
	else
	{
		archive << directory;
		archive << meshfile;
		archive << materialfile;
		archive << loaded;
	}
}
#pragma endregion

#pragma region MATERIAL
Material::~Material() {
	wiResourceManager::GetGlobal()->del(refMapName);
	wiResourceManager::GetGlobal()->del(textureName);
	wiResourceManager::GetGlobal()->del(normalMapName);
	wiResourceManager::GetGlobal()->del(displacementMapName);
	wiResourceManager::GetGlobal()->del(specularMapName);
	refMap = nullptr;
	texture = nullptr;
	normalMap = nullptr;
	displacementMap = nullptr;
	specularMap = nullptr;
}
void Material::ConvertToPhysicallyBasedMaterial()
{
	baseColor = diffuseColor;
	roughness = (1 - (float)specular_power / 128.0f);
	metalness = 0.0f;
	reflectance = (specular.x + specular.y + specular.z) / 3.0f * specular.w;
	normalMapStrength = 1.0f;
}
const Texture2D* Material::GetBaseColorMap() const
{
	if (texture != nullptr)
	{
		return texture;
	}
	return wiTextureHelper::getInstance()->getWhite();
}
const Texture2D* Material::GetNormalMap() const
{
	return normalMap;
	//if (normalMap != nullptr)
	//{
	//	return normalMap;
	//}
	//return wiTextureHelper::getInstance()->getNormalMapDefault();
}
const Texture2D* Material::GetRoughnessMap() const
{
	return wiTextureHelper::getInstance()->getWhite();
}
const Texture2D* Material::GetMetalnessMap() const
{
	return wiTextureHelper::getInstance()->getWhite();
}
const Texture2D* Material::GetReflectanceMap() const
{
	if (refMap != nullptr)
	{
		return refMap;
	}
	return wiTextureHelper::getInstance()->getWhite();
}
const Texture2D* Material::GetDisplacementMap() const
{
	if (displacementMap != nullptr)
	{
		return displacementMap;
	}
	return wiTextureHelper::getInstance()->getWhite();
}
void Material::Serialize(wiArchive& archive)
{
	if (archive.IsReadMode())
	{
		archive >> name;
		archive >> refMapName;
		archive >> textureName;
		archive >> premultipliedTexture;
		int temp;
		archive >> temp;
		blendFlag = (BLENDMODE)temp;
		archive >> normalMapName;
		archive >> displacementMapName;
		archive >> specularMapName;
		archive >> water;
		archive >> movingTex;
		archive >> framesToWaitForTexCoordOffset;
		archive >> texMulAdd;
		archive >> cast_shadow;
		archive >> baseColor;
		archive >> alpha;
		archive >> roughness;
		archive >> reflectance;
		archive >> metalness;
		archive >> emissive;
		archive >> refractionIndex;
		archive >> subsurfaceScattering;
		archive >> normalMapStrength;
		if (archive.GetVersion() >= 2)
		{
			archive >> planar_reflections;
		}
		if (archive.GetVersion() >= 3)
		{
			archive >> parallaxOcclusionMapping;
		}
		if (archive.GetVersion() >= 4)
		{
			archive >> alphaRef;
		}

		string texturesDir = archive.GetSourceDirectory() + "textures/";
		if (!refMapName.empty())
		{
			refMapName = texturesDir + refMapName;
			refMap = (Texture2D*)wiResourceManager::GetGlobal()->add(refMapName);
		}
		if (!textureName.empty())
		{
			textureName = texturesDir + textureName;
			texture = (Texture2D*)wiResourceManager::GetGlobal()->add(textureName);
		}
		if (!normalMapName.empty())
		{
			normalMapName = texturesDir + normalMapName;
			normalMap = (Texture2D*)wiResourceManager::GetGlobal()->add(normalMapName);
		}
		if (!displacementMapName.empty())
		{
			displacementMapName = texturesDir + displacementMapName;
			displacementMap = (Texture2D*)wiResourceManager::GetGlobal()->add(displacementMapName);
		}
		if (!specularMapName.empty())
		{
			specularMapName = texturesDir + specularMapName;
			specularMap = (Texture2D*)wiResourceManager::GetGlobal()->add(specularMapName);
		}
	}
	else
	{
		archive << name;
		archive << wiHelper::GetFileNameFromPath(refMapName);
		archive << wiHelper::GetFileNameFromPath(textureName);
		archive << premultipliedTexture;
		archive << (int)blendFlag;
		archive << wiHelper::GetFileNameFromPath(normalMapName);
		archive << wiHelper::GetFileNameFromPath(displacementMapName);
		archive << wiHelper::GetFileNameFromPath(specularMapName);
		archive << water;
		archive << movingTex;
		archive << framesToWaitForTexCoordOffset;
		archive << texMulAdd;
		archive << cast_shadow;
		archive << baseColor;
		archive << alpha;
		archive << roughness;
		archive << reflectance;
		archive << metalness;
		archive << emissive;
		archive << refractionIndex;
		archive << subsurfaceScattering;
		archive << normalMapStrength;
		if (archive.GetVersion() >= 2)
		{
			archive << planar_reflections;
		}
		if (archive.GetVersion() >= 3)
		{
			archive << parallaxOcclusionMapping;
		}
		if (archive.GetVersion() >= 4)
		{
			archive << alphaRef;
		}
	}
}
#pragma endregion

#pragma region MESHSUBSET

MeshSubset::MeshSubset()
{
	material = nullptr;
}
MeshSubset::~MeshSubset()
{
}

#pragma endregion

#pragma region VERTEXGROUP
void VertexGroup::Serialize(wiArchive& archive)
{
	if (archive.IsReadMode())
	{
		archive >> name;
		size_t vertexCount;
		archive >> vertexCount;
		for (size_t i = 0; i < vertexCount; ++i)
		{
			int first;
			float second;
			archive >> first;
			archive >> second;
			vertices.insert(pair<int, float>(first, second));
		}
	}
	else
	{
		archive << name;
		archive << vertices.size();
		for (auto& x : vertices)
		{
			archive << x.first;
			archive << x.second;
		}
	}
}
#pragma endregion

#pragma region MESH

GPUBuffer Mesh::impostorVB;

void Mesh::LoadFromFile(const string& newName, const string& fname
	, const MaterialCollection& materialColl, list<Armature*> armatures, const string& identifier) {
	name = newName;

	BYTE* buffer;
	size_t fileSize;
	if (wiHelper::readByteData(fname, &buffer, fileSize)) {

		int offset = 0;

		int VERSION;
		memcpy(&VERSION, buffer, sizeof(int));
		offset += sizeof(int);

		if (VERSION >= 1001) {
			int doubleside;
			memcpy(&doubleside, buffer + offset, sizeof(int));
			offset += sizeof(int);
			if (doubleside) {
				doubleSided = true;
			}
		}

		int billboard;
		memcpy(&billboard, buffer + offset, sizeof(int));
		offset += sizeof(int);
		if (billboard) {
			char axis;
			memcpy(&axis, buffer + offset, 1);
			offset += 1;

			if (toupper(axis) == 'Y')
				billboardAxis = XMFLOAT3(0, 1, 0);
			else if (toupper(axis) == 'X')
				billboardAxis = XMFLOAT3(1, 0, 0);
			else if (toupper(axis) == 'Z')
				billboardAxis = XMFLOAT3(0, 0, 1);
			else
				billboardAxis = XMFLOAT3(0, 0, 0);
			isBillboarded = true;
		}

		int parented; //parentnamelength
		memcpy(&parented, buffer + offset, sizeof(int));
		offset += sizeof(int);
		if (parented) {
			char* pName = new char[parented + 1]();
			memcpy(pName, buffer + offset, parented);
			offset += parented;
			parent = pName;
			delete[] pName;

			stringstream identified_parent("");
			identified_parent << parent << identifier;
			for (Armature* a : armatures) {
				if (!a->name.compare(identified_parent.str())) {
					armatureName = identified_parent.str();
					armature = a;
				}
			}
		}

		int materialCount;
		memcpy(&materialCount, buffer + offset, sizeof(int));
		offset += sizeof(int);
		for (int i = 0; i<materialCount; ++i) {
			int matNameLen;
			memcpy(&matNameLen, buffer + offset, sizeof(int));
			offset += sizeof(int);
			char* matName = new char[matNameLen + 1]();
			memcpy(matName, buffer + offset, matNameLen);
			offset += matNameLen;

			stringstream identified_matname("");
			identified_matname << matName << identifier;
			MaterialCollection::const_iterator iter = materialColl.find(identified_matname.str());
			if (iter != materialColl.end()) {
				subsets.push_back(MeshSubset());
				subsets.back().material = iter->second;
				//materials.push_back(iter->second);
			}

			materialNames.push_back(identified_matname.str());
			delete[] matName;
		}
		int rendermesh, vertexCount;
		memcpy(&rendermesh, buffer + offset, sizeof(int));
		offset += sizeof(int);
		memcpy(&vertexCount, buffer + offset, sizeof(int));
		offset += sizeof(int);

		vertices.reserve(vertexCount);
		for (int i = 0; i<vertexCount; ++i) {
			SkinnedVertex vert = SkinnedVertex();
			float v[8];
			memcpy(v, buffer + offset, sizeof(float) * 8);
			offset += sizeof(float) * 8;
			vert.pos.x = v[0];
			vert.pos.y = v[1];
			vert.pos.z = v[2];
			if (!isBillboarded) {
				vert.nor.x = v[3];
				vert.nor.y = v[4];
				vert.nor.z = v[5];
			}
			else {
				vert.nor.x = billboardAxis.x;
				vert.nor.y = billboardAxis.y;
				vert.nor.z = billboardAxis.z;
			}
			vert.tex.x = v[6];
			vert.tex.y = v[7];
			int matIndex;
			memcpy(&matIndex, buffer + offset, sizeof(int));
			offset += sizeof(int);
			vert.tex.z = (float)matIndex;

			int weightCount = 0;
			memcpy(&weightCount, buffer + offset, sizeof(int));
			offset += sizeof(int);
			for (int j = 0; j<weightCount; ++j) {

				int weightNameLen = 0;
				memcpy(&weightNameLen, buffer + offset, sizeof(int));
				offset += sizeof(int);
				char* weightName = new char[weightNameLen + 1](); //bone name
				memcpy(weightName, buffer + offset, weightNameLen);
				offset += weightNameLen;
				float weightValue = 0;
				memcpy(&weightValue, buffer + offset, sizeof(float));
				offset += sizeof(float);

#pragma region BONE INDEX SETUP
				string nameB = weightName;
				if (armature) {
					bool gotBone = false;
					int BONEINDEX = 0;
					int b = 0;
					while (!gotBone && b<(int)armature->boneCollection.size()) {
						if (!armature->boneCollection[b]->name.compare(nameB)) {
							gotBone = true;
							BONEINDEX = b; //GOT INDEX OF BONE OF THE WEIGHT IN THE PARENT ARMATURE
						}
						b++;
					}
					if (gotBone) { //ONLY PROCEED IF CORRESPONDING BONE WAS FOUND
						if (!vert.wei.x) {
							vert.wei.x = weightValue;
							vert.bon.x = (float)BONEINDEX;
						}
						else if (!vert.wei.y) {
							vert.wei.y = weightValue;
							vert.bon.y = (float)BONEINDEX;
						}
						else if (!vert.wei.z) {
							vert.wei.z = weightValue;
							vert.bon.z = (float)BONEINDEX;
						}
						else if (!vert.wei.w) {
							vert.wei.w = weightValue;
							vert.bon.w = (float)BONEINDEX;
						}
					}
				}

				//(+RIBBONTRAIL SETUP)(+VERTEXGROUP SETUP)

				if (nameB.find("trailbase") != string::npos)
					trailInfo.base = (int)vertices.size();
				else if (nameB.find("trailtip") != string::npos)
					trailInfo.tip = (int)vertices.size();

				bool windAffection = false;
				if (nameB.find("wind") != string::npos)
					windAffection = true;
				bool gotvg = false;
				for (unsigned int v = 0; v<vertexGroups.size(); ++v)
					if (!nameB.compare(vertexGroups[v].name)) {
						gotvg = true;
						vertexGroups[v].addVertex(VertexRef((int)vertices.size(), weightValue));
						if (windAffection)
							vert.tex.w = weightValue;
					}
				if (!gotvg) {
					vertexGroups.push_back(VertexGroup(nameB));
					vertexGroups.back().addVertex(VertexRef((int)vertices.size(), weightValue));
					if (windAffection)
						vert.tex.w = weightValue;
				}
#pragma endregion

				delete[] weightName;


			}


			vertices.push_back(vert);
		}

		if (rendermesh) {
			int indexCount;
			memcpy(&indexCount, buffer + offset, sizeof(int));
			offset += sizeof(int);
			unsigned int* indexArray = new unsigned int[indexCount];
			memcpy(indexArray, buffer + offset, sizeof(unsigned int)*indexCount);
			offset += sizeof(unsigned int)*indexCount;
			indices.reserve(indexCount);
			for (int i = 0; i<indexCount; ++i) {
				indices.push_back(indexArray[i]);
			}
			delete[] indexArray;

			int softBody;
			memcpy(&softBody, buffer + offset, sizeof(int));
			offset += sizeof(int);
			if (softBody) {
				int softCount[2]; //ind,vert
				memcpy(softCount, buffer + offset, sizeof(int) * 2);
				offset += sizeof(int) * 2;
				unsigned int* softind = new unsigned int[softCount[0]];
				memcpy(softind, buffer + offset, sizeof(unsigned int)*softCount[0]);
				offset += sizeof(unsigned int)*softCount[0];
				float* softvert = new float[softCount[1]];
				memcpy(softvert, buffer + offset, sizeof(float)*softCount[1]);
				offset += sizeof(float)*softCount[1];

				physicsindices.reserve(softCount[0]);
				physicsverts.reserve(softCount[1] / 3);
				for (int i = 0; i<softCount[0]; ++i) {
					physicsindices.push_back(softind[i]);
				}
				for (int i = 0; i<softCount[1]; i += 3) {
					physicsverts.push_back(XMFLOAT3(softvert[i], softvert[i + 1], softvert[i + 2]));
				}

				delete[] softind;
				delete[] softvert;
			}
			else {

			}
		}
		else {

		}

		memcpy(aabb.corners, buffer + offset, sizeof(aabb.corners));
		offset += sizeof(aabb.corners);

		int isSoftbody;
		memcpy(&isSoftbody, buffer + offset, sizeof(int));
		offset += sizeof(int);
		if (isSoftbody) {
			float prop[2]; //mass,friction
			memcpy(prop, buffer + offset, sizeof(float) * 2);
			offset += sizeof(float) * 2;
			softBody = true;
			mass = prop[0];
			friction = prop[1];
			int vglenghts[3]; //goal,mass,soft
			memcpy(vglenghts, buffer + offset, sizeof(int) * 3);
			offset += sizeof(int) * 3;

			char* vgg = new char[vglenghts[0] + 1]();
			char* vgm = new char[vglenghts[1] + 1]();
			char* vgs = new char[vglenghts[2] + 1]();

			memcpy(vgg, buffer + offset, vglenghts[0]);
			offset += vglenghts[0];
			memcpy(vgm, buffer + offset, vglenghts[1]);
			offset += vglenghts[1];
			memcpy(vgs, buffer + offset, vglenghts[2]);
			offset += vglenghts[2];

			for (unsigned int v = 0; v<vertexGroups.size(); ++v) {
				if (!strcmp(vgm, vertexGroups[v].name.c_str()))
					massVG = v;
				if (!strcmp(vgg, vertexGroups[v].name.c_str()))
					goalVG = v;
				if (!strcmp(vgs, vertexGroups[v].name.c_str()))
					softVG = v;
			}

			delete[]vgg;
			delete[]vgm;
			delete[]vgs;
		}

		delete[] buffer;

		renderable = rendermesh == 0 ? false : true;
	}
}
void Mesh::Optimize()
{
	//TODO
}
void Mesh::CreateBuffers(Object* object) {
	if (!buffersComplete) 
	{

		GPUBufferDesc bd;
		if (!instanceBuffer.IsValid())
		{
			ZeroMemory(&bd, sizeof(bd));
			bd.Usage = USAGE_DYNAMIC;
			bd.ByteWidth = sizeof(Instance);
			bd.BindFlags = BIND_VERTEX_BUFFER;
			bd.CPUAccessFlags = CPU_ACCESS_WRITE;
			wiRenderer::GetDevice()->CreateBuffer(&bd, nullptr, &instanceBuffer);
		}


		if (goalVG >= 0) {
			goalPositions.resize(vertexGroups[goalVG].vertices.size());
			goalNormals.resize(vertexGroups[goalVG].vertices.size());
		}

		ZeroMemory(&bd, sizeof(bd));
#ifdef USE_GPU_SKINNING
		bd.Usage = (softBody ? USAGE_DYNAMIC : USAGE_IMMUTABLE);
		bd.CPUAccessFlags = (softBody ? CPU_ACCESS_WRITE : 0);
		if (object->isArmatureDeformed() && !softBody)
			bd.ByteWidth = (UINT)(sizeof(SkinnedVertex) * vertices.size());
		else
			bd.ByteWidth = (UINT)(sizeof(Vertex) * vertices_Complete.size());
#else
		bd.Usage = ((softBody || object->isArmatureDeformed()) ? USAGE_DYNAMIC : USAGE_IMMUTABLE);
		bd.CPUAccessFlags = ((softBody || object->isArmatureDeformed()) ? CPU_ACCESS_WRITE : 0);
		bd.ByteWidth = sizeof(Vertex) * vertices_Complete.size();
#endif
		bd.BindFlags = BIND_VERTEX_BUFFER;
		SubresourceData InitData;
		ZeroMemory(&InitData, sizeof(InitData));
		if (object->isArmatureDeformed() && !softBody)
			InitData.pSysMem = vertices.data();
		else
			InitData.pSysMem = vertices_Complete.data();
		wiRenderer::GetDevice()->CreateBuffer(&bd, &InitData, &vertexBuffer);

		if (renderable)
		{
			if (object->isArmatureDeformed() && !softBody) {
				ZeroMemory(&bd, sizeof(bd));
				bd.Usage = USAGE_DEFAULT;
				bd.ByteWidth = (UINT)(sizeof(Vertex) * vertices_Complete.size());
				bd.BindFlags = BIND_STREAM_OUTPUT | BIND_VERTEX_BUFFER;
				bd.CPUAccessFlags = 0;
				bd.StructureByteStride = 0;
				wiRenderer::GetDevice()->CreateBuffer(&bd, nullptr, &streamoutBuffer);
			}

			//PHYSICALMAPPING
			if (!physicsverts.empty() && physicalmapGP.empty())
			{
				for (unsigned int i = 0; i < vertices.size(); ++i) {
					for (unsigned int j = 0; j < physicsverts.size(); ++j) {
						if (fabs(vertices[i].pos.x - physicsverts[j].x) < FLT_EPSILON
							&&	fabs(vertices[i].pos.y - physicsverts[j].y) < FLT_EPSILON
							&&	fabs(vertices[i].pos.z - physicsverts[j].z) < FLT_EPSILON
							)
						{
							physicalmapGP.push_back(j);
							break;
						}
					}
				}
			}
		}

		for (MeshSubset& subset : subsets)
		{
			if (subset.subsetIndices.empty())
			{
				continue;
			}
			ZeroMemory(&bd, sizeof(bd));
			bd.Usage = USAGE_IMMUTABLE;
			bd.ByteWidth = (UINT)(sizeof(unsigned int) * subset.subsetIndices.size());
			bd.BindFlags = BIND_INDEX_BUFFER;
			bd.CPUAccessFlags = 0;
			ZeroMemory(&InitData, sizeof(InitData));
			InitData.pSysMem = subset.subsetIndices.data();
			wiRenderer::GetDevice()->CreateBuffer(&bd, &InitData, &subset.indexBuffer);
		}


		buffersComplete = true;
	}

}
void Mesh::CreateImpostorVB()
{
	if (!impostorVB.IsValid())
	{
		Vertex impostorVertices[6 * 6];

		float stepX = 1.f / 6.f;

		// front
		impostorVertices[0].pos = XMFLOAT4(-1, 1, 0, 1);
		impostorVertices[0].nor = XMFLOAT4(0, 0, -1, 1);
		impostorVertices[0].tex = XMFLOAT4(0, 0, 0, 0);
		impostorVertices[0].pre = XMFLOAT4(-1, 1, 0, 1);

		impostorVertices[1].pos = XMFLOAT4(-1, -1, 0, 1);
		impostorVertices[1].nor = XMFLOAT4(0, 0, -1, 1);
		impostorVertices[1].tex = XMFLOAT4(0, 1, 0, 0);
		impostorVertices[1].pre = XMFLOAT4(-1, -1, 0, 1);

		impostorVertices[2].pos = XMFLOAT4(1, 1, 0, 1);
		impostorVertices[2].nor = XMFLOAT4(0, 0, -1, 1);
		impostorVertices[2].tex = XMFLOAT4(stepX, 0, 0, 0);
		impostorVertices[2].pre = XMFLOAT4(1, 1, 0, 1);

		impostorVertices[3].pos = XMFLOAT4(-1, -1, 0, 1);
		impostorVertices[3].nor = XMFLOAT4(0, 0, -1, 1);
		impostorVertices[3].tex = XMFLOAT4(0, 1, 0, 0);
		impostorVertices[3].pre = XMFLOAT4(-1, -1, 0, 1);

		impostorVertices[4].pos = XMFLOAT4(1, -1, 0, 1);
		impostorVertices[4].nor = XMFLOAT4(0, 0, -1, 1);
		impostorVertices[4].tex = XMFLOAT4(stepX, 1, 0, 0);
		impostorVertices[4].pre = XMFLOAT4(1, -1, 0, 1);

		impostorVertices[5].pos = XMFLOAT4(1, 1, 0, 1);
		impostorVertices[5].nor = XMFLOAT4(0, 0, -1, 1);
		impostorVertices[5].tex = XMFLOAT4(stepX, 0, 0, 0);
		impostorVertices[5].pre = XMFLOAT4(1, 1, 0, 1);

		// right
		impostorVertices[6].pos = XMFLOAT4(0, 1, -1, 1);
		impostorVertices[6].nor = XMFLOAT4(1, 0, 0, 1);
		impostorVertices[6].tex = XMFLOAT4(stepX, 0, 0, 0);
		impostorVertices[6].pre = XMFLOAT4(0, 1, -1, 1);

		impostorVertices[7].pos = XMFLOAT4(0, -1, -1, 1);
		impostorVertices[7].nor = XMFLOAT4(1, 0, 0, 1);
		impostorVertices[7].tex = XMFLOAT4(stepX, 1, 0, 0);
		impostorVertices[7].pre = XMFLOAT4(0, -1, -1, 1);

		impostorVertices[8].pos = XMFLOAT4(0, 1, 1, 1);
		impostorVertices[8].nor = XMFLOAT4(1, 0, 0, 1);
		impostorVertices[8].tex = XMFLOAT4(stepX*2, 0, 0, 0);
		impostorVertices[8].pre = XMFLOAT4(0, 1, 1, 1);

		impostorVertices[9].pos = XMFLOAT4(0, -1, -1, 1);
		impostorVertices[9].nor = XMFLOAT4(1, 0, 0, 1);
		impostorVertices[9].tex = XMFLOAT4(stepX, 1, 0, 0);
		impostorVertices[9].pre = XMFLOAT4(0, -1, -1, 1);

		impostorVertices[10].pos = XMFLOAT4(0, -1, 1, 1);
		impostorVertices[10].nor = XMFLOAT4(1, 0, 0, 1);
		impostorVertices[10].tex = XMFLOAT4(stepX*2, 1, 0, 0);
		impostorVertices[10].pre = XMFLOAT4(0, -1, 1, 1);

		impostorVertices[11].pos = XMFLOAT4(0, 1, 1, 1);
		impostorVertices[11].nor = XMFLOAT4(1, 0, 0, 1);
		impostorVertices[11].tex = XMFLOAT4(stepX*2, 0, 0, 0);
		impostorVertices[11].pre = XMFLOAT4(0, 1, 1, 1);

		// back
		impostorVertices[12].pos = XMFLOAT4(-1, 1, 0, 1);
		impostorVertices[12].nor = XMFLOAT4(0, 0, -1, 1);
		impostorVertices[12].tex = XMFLOAT4(stepX*3, 0, 0, 0);
		impostorVertices[12].pre = XMFLOAT4(-1, 1, 0, 1);

		impostorVertices[13].pos = XMFLOAT4(1, 1, 0, 1);
		impostorVertices[13].nor = XMFLOAT4(0, 0, -1, 1);
		impostorVertices[13].tex = XMFLOAT4(stepX * 2, 0, 0, 0);
		impostorVertices[13].pre = XMFLOAT4(1, 1, 0, 1);

		impostorVertices[14].pos = XMFLOAT4(-1, -1, 0, 1);
		impostorVertices[14].nor = XMFLOAT4(0, 0, -1, 1);
		impostorVertices[14].tex = XMFLOAT4(stepX*3, 1, 0, 0);
		impostorVertices[14].pre = XMFLOAT4(-1, -1, 0, 1);

		impostorVertices[15].pos = XMFLOAT4(-1, -1, 0, 1);
		impostorVertices[15].nor = XMFLOAT4(0, 0, -1, 1);
		impostorVertices[15].tex = XMFLOAT4(stepX*3, 1, 0, 0);
		impostorVertices[15].pre = XMFLOAT4(-1, -1, 0, 1);

		impostorVertices[16].pos = XMFLOAT4(1, 1, 0, 1);
		impostorVertices[16].nor = XMFLOAT4(0, 0, -1, 1);
		impostorVertices[16].tex = XMFLOAT4(stepX*2, 0, 0, 0);
		impostorVertices[16].pre = XMFLOAT4(1, 1, 0, 1);

		impostorVertices[17].pos = XMFLOAT4(1, -1, 0, 1);
		impostorVertices[17].nor = XMFLOAT4(0, 0, -1, 1);
		impostorVertices[17].tex = XMFLOAT4(stepX*2, 1, 0, 0);
		impostorVertices[17].pre = XMFLOAT4(1, -1, 0, 1);

		// left
		impostorVertices[18].pos = XMFLOAT4(0, 1, -1, 1);
		impostorVertices[18].nor = XMFLOAT4(1, 0, 0, 1);
		impostorVertices[18].tex = XMFLOAT4(stepX*4, 0, 0, 0);
		impostorVertices[18].pre = XMFLOAT4(0, 1, -1, 1);

		impostorVertices[19].pos = XMFLOAT4(0, 1, 1, 1);
		impostorVertices[19].nor = XMFLOAT4(1, 0, 0, 1);
		impostorVertices[19].tex = XMFLOAT4(stepX * 3, 0, 0, 0);
		impostorVertices[19].pre = XMFLOAT4(0, 1, 1, 1);

		impostorVertices[20].pos = XMFLOAT4(0, -1, -1, 1);
		impostorVertices[20].nor = XMFLOAT4(1, 0, 0, 1);
		impostorVertices[20].tex = XMFLOAT4(stepX*4, 1, 0, 0);
		impostorVertices[20].pre = XMFLOAT4(0, -1, -1, 1);

		impostorVertices[21].pos = XMFLOAT4(0, -1, -1, 1);
		impostorVertices[21].nor = XMFLOAT4(1, 0, 0, 1);
		impostorVertices[21].tex = XMFLOAT4(stepX*4, 1, 0, 0);
		impostorVertices[21].pre = XMFLOAT4(0, -1, -1, 1);

		impostorVertices[22].pos = XMFLOAT4(0, 1, 1, 1);
		impostorVertices[22].nor = XMFLOAT4(1, 0, 0, 1);
		impostorVertices[22].tex = XMFLOAT4(stepX*3, 0, 0, 0);
		impostorVertices[22].pre = XMFLOAT4(0, 1, 1, 1);

		impostorVertices[23].pos = XMFLOAT4(0, -1, 1, 1);
		impostorVertices[23].nor = XMFLOAT4(1, 0, 0, 1);
		impostorVertices[23].tex = XMFLOAT4(stepX*3, 1, 0, 0);
		impostorVertices[23].pre = XMFLOAT4(0, -1, 1, 1);

		// bottom
		impostorVertices[24].pos = XMFLOAT4(-1, 0, 1, 1);
		impostorVertices[24].nor = XMFLOAT4(0, 1, 0, 1);
		impostorVertices[24].tex = XMFLOAT4(stepX*4, 0, 0, 0);
		impostorVertices[24].pre = XMFLOAT4(-1, 0, 1, 1);

		impostorVertices[25].pos = XMFLOAT4(1, 0, 1, 1);
		impostorVertices[25].nor = XMFLOAT4(0, 1, 0, 1);
		impostorVertices[25].tex = XMFLOAT4(stepX * 5, 0, 0, 0);
		impostorVertices[25].pre = XMFLOAT4(1, 0, 1, 1);

		impostorVertices[26].pos = XMFLOAT4(-1, 0, -1, 1);
		impostorVertices[26].nor = XMFLOAT4(0, 1, 0, 1);
		impostorVertices[26].tex = XMFLOAT4(stepX*4, 1, 0, 0);
		impostorVertices[26].pre = XMFLOAT4(-1, 0, -1, 1);

		impostorVertices[27].pos = XMFLOAT4(-1, 0, -1, 1);
		impostorVertices[27].nor = XMFLOAT4(0, 1, 0, 1);
		impostorVertices[27].tex = XMFLOAT4(stepX*4, 1, 0, 0);
		impostorVertices[27].pre = XMFLOAT4(-1, 0, -1, 1);

		impostorVertices[28].pos = XMFLOAT4(1, 0, 1, 1);
		impostorVertices[28].nor = XMFLOAT4(0, 1, 0, 1);
		impostorVertices[28].tex = XMFLOAT4(stepX*5, 0, 0, 0);
		impostorVertices[28].pre = XMFLOAT4(1, 0, 1, 1);

		impostorVertices[29].pos = XMFLOAT4(1, 0, -1, 1);
		impostorVertices[29].nor = XMFLOAT4(0, 1, 0, 1);
		impostorVertices[29].tex = XMFLOAT4(stepX*5, 1, 0, 0);
		impostorVertices[29].pre = XMFLOAT4(1, 0, -1, 1);

		// top
		impostorVertices[30].pos = XMFLOAT4(-1, 0, 1, 1);
		impostorVertices[30].nor = XMFLOAT4(0, 1, 0, 1);
		impostorVertices[30].tex = XMFLOAT4(stepX*5, 0, 0, 0);
		impostorVertices[30].pre = XMFLOAT4(-1, 0, 1, 1);

		impostorVertices[31].pos = XMFLOAT4(-1, 0, -1, 1);
		impostorVertices[31].nor = XMFLOAT4(0, 1, 0, 1);
		impostorVertices[31].tex = XMFLOAT4(stepX * 5, 1, 0, 0);
		impostorVertices[31].pre = XMFLOAT4(-1, 0, -1, 1);

		impostorVertices[32].pos = XMFLOAT4(1, 0, 1, 1);
		impostorVertices[32].nor = XMFLOAT4(0, 1, 0, 1);
		impostorVertices[32].tex = XMFLOAT4(stepX*6, 0, 0, 0);
		impostorVertices[32].pre = XMFLOAT4(1, 0, 1, 1);

		impostorVertices[33].pos = XMFLOAT4(-1, 0, -1, 1);
		impostorVertices[33].nor = XMFLOAT4(0, 1, 0, 1);
		impostorVertices[33].tex = XMFLOAT4(stepX*5, 1, 0, 0);
		impostorVertices[33].pre = XMFLOAT4(-1, 0, -1, 1);

		impostorVertices[34].pos = XMFLOAT4(1, 0, -1, 1);
		impostorVertices[34].nor = XMFLOAT4(0, 1, 0, 1);
		impostorVertices[34].tex = XMFLOAT4(stepX*6, 1, 0, 0);
		impostorVertices[34].pre = XMFLOAT4(1, 0, -1, 1);

		impostorVertices[35].pos = XMFLOAT4(1, 0, 1, 1);
		impostorVertices[35].nor = XMFLOAT4(0, 1, 0, 1);
		impostorVertices[35].tex = XMFLOAT4(stepX*6, 0, 0, 0);
		impostorVertices[35].pre = XMFLOAT4(1, 0, 1, 1);

		GPUBufferDesc bd;
		ZeroMemory(&bd, sizeof(bd));
		bd.Usage = USAGE_IMMUTABLE;
		bd.ByteWidth = sizeof(impostorVertices);
		bd.BindFlags = BIND_VERTEX_BUFFER;
		bd.CPUAccessFlags = 0;
		SubresourceData InitData;
		ZeroMemory(&InitData, sizeof(InitData));
		InitData.pSysMem = impostorVertices;
		wiRenderer::GetDevice()->CreateBuffer(&bd, &InitData, &impostorVB);
	}
}
void Mesh::CreateVertexArrays()
{
	if (arraysComplete)
	{
		return;
	}

	if (vertices_Complete.empty())
	{
		vertices_Complete.resize(vertices.size());
		for (size_t i = 0; i < vertices.size(); ++i) 
		{
			vertices_Complete[i].pos = vertices[i].pos;
			vertices_Complete[i].nor = vertices[i].nor;
			vertices_Complete[i].tex = vertices[i].tex;
		}
	}

	for (size_t i = 0; i < indices.size(); ++i)
	{
		unsigned int index = indices[i];
		SkinnedVertex skinnedVertex = vertices[index];
		unsigned int materialIndex = (unsigned int)floor(skinnedVertex.tex.z);

		assert((materialIndex < (unsigned int)subsets.size()) && "Bad subset index!");

		MeshSubset& subset = subsets[materialIndex];
		subset.subsetIndices.push_back(index);
	}

	arraysComplete = true;
}

vector<Instance> meshInstances[GRAPHICSTHREAD_COUNT];
void Mesh::AddRenderableInstance(const Instance& instance, int numerator, GRAPHICSTHREAD threadID)
{
	if (numerator >= (int)meshInstances[threadID].size())
	{
		meshInstances[threadID].resize((meshInstances[threadID].size() + 1) * 2);
	}
	if (!instanceBufferIsUpToDate || memcmp(&meshInstances[threadID][numerator], &instance, sizeof(Instance)) != 0)
	{
		instanceBufferIsUpToDate = false;
		meshInstances[threadID][numerator] = instance;
	}
}
void Mesh::UpdateRenderableInstances(int count, GRAPHICSTHREAD threadID)
{
	if (!instanceBufferIsUpToDate)
	{
		instanceBufferIsUpToDate = true;
		wiRenderer::GetDevice()->UpdateBuffer(&instanceBuffer, meshInstances[threadID].data(), threadID, sizeof(Instance)*count);
	}
}
void Mesh::Serialize(wiArchive& archive)
{
	if (archive.IsReadMode())
	{
		archive >> name;
		archive >> parent;

		// vertices
		{
			size_t vertexCount;
			archive >> vertexCount;
			SkinnedVertex tempVert;
			for (size_t i = 0; i < vertexCount; ++i)
			{
				tempVert.Serialize(archive);
				vertices.push_back(tempVert);
			}
		}
		// indices
		{
			size_t indexCount;
			archive >> indexCount;
			unsigned int tempInd;
			for (size_t i = 0; i < indexCount; ++i)
			{
				archive >> tempInd;
				indices.push_back(tempInd);
			}
		}
		// physicsVerts
		{
			size_t physicsVertCount;
			archive >> physicsVertCount;
			XMFLOAT3 tempPhysicsVert;
			for (size_t i = 0; i < physicsVertCount; ++i)
			{
				archive >> tempPhysicsVert;
				physicsverts.push_back(tempPhysicsVert);
			}
		}
		// physicsindices
		{
			size_t physicsIndexCount;
			archive >> physicsIndexCount;
			unsigned int tempInd;
			for (size_t i = 0; i < physicsIndexCount; ++i)
			{
				archive >> tempInd;
				physicsindices.push_back(tempInd);
			}
		}
		// physicalmapGP
		{
			size_t physicalmapGPCount;
			archive >> physicalmapGPCount;
			int tempInd;
			for (size_t i = 0; i < physicalmapGPCount; ++i)
			{
				archive >> tempInd;
				physicalmapGP.push_back(tempInd);
			}
		}
		// subsets
		{
			size_t subsetCount;
			archive >> subsetCount;
			for (size_t i = 0; i < subsetCount; ++i)
			{
				subsets.push_back(MeshSubset());
			}
		}
		// vertexGroups
		{
			size_t groupCount;
			archive >> groupCount;
			for (size_t i = 0; i < groupCount; ++i)
			{
				VertexGroup group = VertexGroup();
				group.Serialize(archive);
				vertexGroups.push_back(group);
			}
		}
		// materialNames
		{
			size_t materialNameCount;
			archive >> materialNameCount;
			string tempString;
			for (size_t i = 0; i < materialNameCount; ++i)
			{
				archive >> tempString;
				materialNames.push_back(tempString);
			}
		}
		archive >> renderable;
		archive >> doubleSided;
		int temp;
		archive >> temp;
		stencilRef = (STENCILREF)temp;
		archive >> calculatedAO;
		archive >> trailInfo.base;
		archive >> trailInfo.tip;
		archive >> isBillboarded;
		archive >> billboardAxis;
		archive >> softBody;
		archive >> mass;
		archive >> friction;
		archive >> massVG;
		archive >> goalVG;
		archive >> softVG;
		archive >> armatureName;
		aabb.Serialize(archive);
	}
	else
	{
		archive << name;
		archive << parent;

		// vertices
		{
			archive << vertices.size();
			for (auto& x : vertices)
			{
				x.Serialize(archive);
			}
		}
		// indices
		{
			archive << indices.size();
			for (auto& x : indices)
			{
				archive << x;
			}
		}
		// physicsverts
		{
			archive << physicsverts.size();
			for (auto& x : physicsverts)
			{
				archive << x;
			}
		}
		// physicsindices
		{
			archive << physicsindices.size();
			for (auto& x : physicsindices)
			{
				archive << x;
			}
		}
		// physicalmapGP
		{
			archive << physicalmapGP.size();
			for (auto& x : physicalmapGP)
			{
				archive << x;
			}
		}
		// subsets
		{
			archive << subsets.size();
		}
		// vertexGroups
		{
			archive << vertexGroups.size();
			for (auto& x : vertexGroups)
			{
				x.Serialize(archive);
			}
		}
		// materialNames
		{
			archive << materialNames.size();
			for (auto& x : materialNames)
			{
				archive << x;
			}
		}
		archive << renderable;
		archive << doubleSided;
		archive << (int)stencilRef;
		archive << calculatedAO;
		archive << trailInfo.base;
		archive << trailInfo.tip;
		archive << isBillboarded;
		archive << billboardAxis;
		archive << softBody;
		archive << mass;
		archive << friction;
		archive << massVG;
		archive << goalVG;
		archive << softVG;
		archive << armatureName;
		aabb.Serialize(archive);
	}
}
#pragma endregion

#pragma region MODEL
Model::Model()
{

}
Model::~Model()
{
	CleanUp();
}
void Model::CleanUp()
{
	for (Armature* x : armatures)
	{
		SAFE_DELETE(x);
	}
	for (Object* x : objects)
	{
		for (wiEmittedParticle* y : x->eParticleSystems)
		{
			SAFE_DELETE(y);
		}
		for (wiHairParticle* y : x->hParticleSystems)
		{
			SAFE_DELETE(y);
		}
		SAFE_DELETE(x);
	}
	for (Light* x : lights)
	{
		SAFE_DELETE(x);
	}
	for (Decal* x : decals)
	{
		SAFE_DELETE(x);
	}
}
void Model::LoadFromDisk(const string& dir, const string& name, const string& identifier)
{
	wiArchive archive(dir + name + ".wimf", true);
	if (archive.IsOpen())
	{
		// New Import if wimf model is available
		this->Serialize(archive);
	}
	else
	{
		// Old Import
		stringstream directory(""), armatureFilePath(""), materialLibFilePath(""), meshesFilePath(""), objectsFilePath("")
			, actionsFilePath(""), lightsFilePath(""), decalsFilePath("");

		directory << dir;
		armatureFilePath << name << ".wia";
		materialLibFilePath << name << ".wim";
		meshesFilePath << name << ".wi";
		objectsFilePath << name << ".wio";
		actionsFilePath << name << ".wiact";
		lightsFilePath << name << ".wil";
		decalsFilePath << name << ".wid";

		LoadWiArmatures(directory.str(), armatureFilePath.str(), identifier, armatures);
		LoadWiMaterialLibrary(directory.str(), materialLibFilePath.str(), identifier, "textures/", materials);
		LoadWiMeshes(directory.str(), meshesFilePath.str(), identifier, meshes, armatures, materials);
		LoadWiObjects(directory.str(), objectsFilePath.str(), identifier, objects, armatures, meshes, materials);
		LoadWiActions(directory.str(), actionsFilePath.str(), identifier, armatures);
		LoadWiLights(directory.str(), lightsFilePath.str(), identifier, lights);
		LoadWiDecals(directory.str(), decalsFilePath.str(), "textures/", decals);

		FinishLoading();
	}
}
void Model::FinishLoading()
{
	vector<Transform*> transforms(0);
	transforms.reserve(armatures.size() + objects.size() + lights.size() + decals.size());

	for (Armature* x : armatures)
	{
		if (x->actions.size() > 1)
		{
			// If it has actions besides the identity, activate the first by default
			x->GetPrimaryAnimation()->ChangeAction(1);
		}
		transforms.push_back(x);
	}
	for (Object* x : objects) {
		transforms.push_back(x);
		for (wiEmittedParticle* e : x->eParticleSystems)
		{
			// If the particle system has light, then register it to the light array (if not already registered!)
			if (e->light != nullptr)
			{
				bool registeredLight = false;
				for (Light* l : lights)
				{
					if (e->light == l)
					{
						registeredLight = true;
					}
				}
				if (!registeredLight)
				{
					lights.push_back(e->light);
				}
			}
		}
	}
	for (Light* x : lights)
	{
		transforms.push_back(x);
	}
	for (Decal* x : decals)
	{
		transforms.push_back(x);
	}


	// Match loaded parenting information
	for (Transform* x : transforms)
	{
		if (x->parent == nullptr)
		{
			for (Transform* y : transforms)
			{
				if (x != y && !x->parentName.empty() && !x->parentName.compare(y->name))
				{
					Transform* parent = y;
					string parentName = parent->name;
					if (!x->boneParent.empty())
					{
						Armature* armature = dynamic_cast<Armature*>(y);
						if (armature != nullptr)
						{
							for (Bone* bone : armature->boneCollection)
							{
								if (!bone->name.compare(x->boneParent))
								{
									parent = bone;
									break;
								}
							}
						}
					}
					// Match parent
					XMFLOAT4X4 saved_parent_rest_inv = x->parent_inv_rest;
					x->attachTo(parent);
					x->parent_inv_rest = saved_parent_rest_inv;
					x->parentName = parentName; // this will ensure that the bone parenting is always resolved as armature->bone
					break;
				}
			}
		}

		// If it has still no parent, then attach to this model!
		if (x->parent == nullptr)
		{
			x->attachTo(this);
		}
	}


	// Set up Render data
	for (Object* x : objects)
	{
		if (x->mesh != nullptr)
		{
			// Ribbon trails
			if (x->mesh->trailInfo.base >= 0 && x->mesh->trailInfo.tip >= 0) {
				GPUBufferDesc bd;
				ZeroMemory(&bd, sizeof(bd));
				bd.Usage = USAGE_DYNAMIC;
				bd.ByteWidth = sizeof(RibbonVertex) * 1000;
				bd.BindFlags = BIND_VERTEX_BUFFER;
				bd.CPUAccessFlags = CPU_ACCESS_WRITE;
				wiRenderer::GetDevice()->CreateBuffer(&bd, NULL, &x->trailBuff);
				x->trailTex = wiTextureHelper::getInstance()->getTransparent();
				x->trailDistortTex = wiTextureHelper::getInstance()->getNormalMapDefault();
			}

			// Mesh renderdata setup
			x->mesh->CreateVertexArrays();
			x->mesh->Optimize();
			x->mesh->CreateBuffers(x);
		}
	}
}
void Model::UpdateModel()
{
	for (MaterialCollection::iterator iter = materials.begin(); iter != materials.end(); ++iter)
	{
		Material* iMat = iter->second;
		iMat->framesToWaitForTexCoordOffset -= 1.0f;
		if (iMat->framesToWaitForTexCoordOffset <= 0) 
		{
			iMat->texMulAdd.z = fmodf(iMat->texMulAdd.z + iMat->movingTex.x*wiRenderer::GetGameSpeed(), 1);
			iMat->texMulAdd.w = fmodf(iMat->texMulAdd.w + iMat->movingTex.y*wiRenderer::GetGameSpeed(), 1);
			iMat->framesToWaitForTexCoordOffset = iMat->movingTex.z*wiRenderer::GetGameSpeed();
		}
	}

	for (Armature* x : armatures)
	{
		x->UpdateArmature();
	}
	for (Object* x : objects)
	{
		x->UpdateObject();
	}
	for (Light*x : lights)
	{
		x->UpdateLight();
	}
	
	list<Decal*>::iterator iter = decals.begin();
	while (iter != decals.end())
	{
		Decal* decal = *iter;
		decal->UpdateDecal();
		if (decal->life>-2) {
			if (decal->life <= 0) {
				decal->detach();
				decals.erase(iter++);
				delete decal;
				continue;
			}
		}
		++iter;
	}
}
void Model::Add(Object* value)
{
	if (value != nullptr)
	{
		objects.push_back(value);
		if (value->mesh != nullptr)
		{
			meshes.insert(pair<string, Mesh*>(value->mesh->name, value->mesh));
			for (auto& x : value->mesh->subsets)
			{
				materials.insert(pair<string, Material*>(x.material->name, x.material));
			}
			this->Add(value->mesh->armature);
		}
	}
}
void Model::Add(Armature* value)
{
	if (value != nullptr)
	{
		armatures.push_back(value);
	}
}
void Model::Add(Light* value)
{
	if (value != nullptr)
	{
		lights.push_back(value);
	}
}
void Model::Add(Decal* value)
{
	if (value != nullptr)
	{
		decals.push_back(value);
	}
}
void Model::Add(Model* value)
{
	if (value != nullptr)
	{
		objects.insert(objects.begin(), value->objects.begin(), value->objects.end());
		armatures.insert(armatures.begin(), value->armatures.begin(), value->armatures.end());
		decals.insert(decals.begin(), value->decals.begin(), value->decals.end());
		lights.insert(lights.begin(), value->lights.begin(), value->lights.end());
		meshes.insert(value->meshes.begin(), value->meshes.end());
		materials.insert(value->materials.begin(), value->materials.end());
	}
}
void Model::Serialize(wiArchive& archive)
{
	Transform::Serialize(archive);

	if (archive.IsReadMode())
	{
		size_t objectsCount, meshCount, materialCount, armaturesCount, lightsCount, decalsCount;

		archive >> objectsCount;
		for (size_t i = 0; i < objectsCount; ++i)
		{
			Object* x = new Object;
			x->Serialize(archive);
			objects.push_back(x);
		}

		archive >> meshCount;
		for (size_t i = 0; i < meshCount; ++i)
		{
			Mesh* x = new Mesh;
			x->Serialize(archive);
			meshes.insert(pair<string, Mesh*>(x->name, x));
		}

		archive >> materialCount;
		for (size_t i = 0; i < materialCount; ++i)
		{
			Material* x = new Material;
			x->Serialize(archive);
			materials.insert(pair<string, Material*>(x->name, x));
		}

		archive >> armaturesCount;
		for (size_t i = 0; i < armaturesCount; ++i)
		{
			Armature* x = new Armature;
			x->Serialize(archive);
			armatures.push_back(x);
		}

		archive >> lightsCount;
		for (size_t i = 0; i < lightsCount; ++i)
		{
			Light* x = new Light;
			x->Serialize(archive);
			lights.push_back(x);
		}

		archive >> decalsCount;
		for (size_t i = 0; i < decalsCount; ++i)
		{
			Decal* x = new Decal;
			x->Serialize(archive);
			decals.push_back(x);
		}

		// RESOLVE CONNECTIONS
		for (Object* x : objects)
		{
			if (x->mesh == nullptr)
			{
				// find mesh
				MeshCollection::iterator found = meshes.find(x->meshfile);
				if (found != meshes.end())
				{
					x->mesh = found->second;
				}
			}
			if (x->mesh != nullptr)
			{
				// find materials for mesh subsets
				int i = 0;
				for (auto& y : x->mesh->subsets)
				{
					if (y.material == nullptr)
					{
						MaterialCollection::iterator it = materials.find(x->mesh->materialNames[i]);
						if (it != materials.end())
						{
							y.material = it->second;
						}
					}
					i++;
				}
				// find armature
				if (!x->mesh->armatureName.empty())
				{
					for (auto& y : armatures)
					{
						if (!y->name.compare(x->mesh->armatureName))
						{
							x->mesh->armature = y;
							break;
						}
					}
				}
			}
			// link particlesystems
			for (auto& y : x->eParticleSystems)
			{
				y->object = x;
				MaterialCollection::iterator it = materials.find(y->materialName);
				if (it != materials.end())
				{
					y->material = it->second;
					if (!y->lightName.empty())
					{
						for (auto& l : lights)
						{
							if (!l->name.compare(y->lightName))
							{
								y->light = l;
								break;
							}
						}
					}
				}
			}
			for (auto& y : x->hParticleSystems)
			{
				y->object = x;
				MaterialCollection::iterator it = materials.find(y->materialName);
				if (it != materials.end())
				{
					y->material = it->second;
					y->SetUpPatches();
				}
			}
		}

		FinishLoading();
	}
	else
	{
		archive << objects.size();
		for (auto& x : objects)
		{
			x->Serialize(archive);
		}

		archive << meshes.size();
		for (auto& x : meshes)
		{
			x.second->Serialize(archive);
		}

		archive << materials.size();
		for (auto& x : materials)
		{
			x.second->Serialize(archive);
		}

		archive << armatures.size();
		for (auto& x : armatures)
		{
			x->Serialize(archive);
		}

		archive << lights.size();
		for (auto& x : lights)
		{
			x->Serialize(archive);
		}

		archive << decals.size();
		for (auto& x : decals)
		{
			x->Serialize(archive);
		}
	}
}
#pragma endregion

#pragma region HITSPHERE
GPUBuffer HitSphere::vertexBuffer;
void HitSphere::SetUpStatic()
{
	const int numVert = (RESOLUTION+1)*2;
	vector<XMFLOAT3A> verts(0);

	for(int i=0;i<=RESOLUTION;++i){
		float alpha = (float)i/(float)RESOLUTION*2*3.14159265359f;
		verts.push_back(XMFLOAT3A(XMFLOAT3A(sin(alpha),cos(alpha),0)));
		verts.push_back(XMFLOAT3A(XMFLOAT3A(0,0,0)));
	}

	GPUBufferDesc bd;
	ZeroMemory( &bd, sizeof(bd) );
	bd.Usage = USAGE_IMMUTABLE;
	bd.ByteWidth = (UINT)(sizeof( XMFLOAT3A )*verts.size());
	bd.BindFlags = BIND_VERTEX_BUFFER;
	bd.CPUAccessFlags = 0;
	SubresourceData InitData;
	ZeroMemory( &InitData, sizeof(InitData) );
	InitData.pSysMem = verts.data();
	wiRenderer::GetDevice()->CreateBuffer( &bd, &InitData, &vertexBuffer );
}
void HitSphere::CleanUpStatic()
{
	
}
void HitSphere::UpdateTransform()
{
	Transform::UpdateTransform();
	
	//getMatrix();
	center = translation;
	radius = radius_saved*scale.x;
}
#pragma endregion

#pragma region BONE
XMMATRIX Bone::getMatrix(int getTranslation, int getRotation, int getScale)
{
	
	return XMMatrixTranslation(0,0,length)*XMLoadFloat4x4(&world);
}
void Bone::UpdateTransform()
{
	//Transform::UpdateTransform();

	// Needs to be updated differently than regular Transforms

	for (Transform* child : children)
	{
		child->UpdateTransform();
	}
}
void Bone::Serialize(wiArchive& archive)
{
	Transform::Serialize(archive);

	if (archive.IsReadMode())
	{
		size_t childCount;
		archive >> childCount;
		string tempName;
		for (size_t j = 0; j < childCount; ++j)
		{
			archive >> tempName;
			childrenN.push_back(tempName);
		}
		archive >> restInv;
		size_t actionFramesCount;
		archive >> actionFramesCount;
		for (size_t i = 0; i < actionFramesCount; ++i)
		{
			ActionFrames aframes = ActionFrames();
			KeyFrame tempKeyFrame;
			size_t tempCount;
			archive >> tempCount;
			for (size_t i = 0; i < tempCount; ++i)
			{
				tempKeyFrame.Serialize(archive);
				aframes.keyframesRot.push_back(tempKeyFrame);
			}
			archive >> tempCount;
			for (size_t i = 0; i < tempCount; ++i)
			{
				tempKeyFrame.Serialize(archive);
				aframes.keyframesPos.push_back(tempKeyFrame);
			}
			archive >> tempCount;
			for (size_t i = 0; i < tempCount; ++i)
			{
				tempKeyFrame.Serialize(archive);
				aframes.keyframesSca.push_back(tempKeyFrame);
			}
			actionFrames.push_back(aframes);
		}
		archive >> recursivePose;
		archive >> recursiveRest;
		archive >> recursiveRestInv;
		archive >> length;
		archive >> connected;
	}
	else
	{
		archive << childrenN.size();
		for (auto& x : childrenN)
		{
			archive << x;
		}
		archive << restInv;
		archive << actionFrames.size();
		int i = 0;
		for (auto& x : actionFrames)
		{
			archive << x.keyframesRot.size();
			for (auto& y : x.keyframesRot)
			{
				y.Serialize(archive);
			}
			archive << x.keyframesPos.size();
			for (auto& y : x.keyframesPos)
			{
				y.Serialize(archive);
			}
			archive << x.keyframesSca.size();
			for (auto& y : x.keyframesSca)
			{
				y.Serialize(archive);
			}
		}
		archive << recursivePose;
		archive << recursiveRest;
		archive << recursiveRestInv;
		archive << length;
		archive << connected;
	}
}
#pragma endregion

#pragma region KEYFRAME
void KeyFrame::Serialize(wiArchive& archive)
{
	if (archive.IsReadMode())
	{
		archive >> data;
		archive >> frameI;
	}
	else
	{
		archive << data;
		archive << frameI;
	}
}
#pragma endregion

#pragma region ANIMATIONLAYER
AnimationLayer::AnimationLayer()
{
	name = "";

	activeAction = prevAction = 0;
	ResetAction();
	ResetActionPrev();

	playing = true;
	blendCurrentFrame = 0.0f;
	blendFrames = 0.0f;
	blendFact = 0.0f;
	currentFramePrevAction = 0.0f;
	weight = 1.0f;
	type = ANIMLAYER_TYPE_ADDITIVE;

	looped = true;
}

void AnimationLayer::ChangeAction(int actionIndex, float blendFrames, float weight)
{
	currentFramePrevAction = currentFrame;
	ResetAction();
	prevAction = activeAction;
	activeAction = actionIndex;
	this->blendFrames = blendFrames;
	blendFact = 0.0f;
	blendCurrentFrame = 0.0f;
	this->weight = weight;
}
void AnimationLayer::ResetAction()
{
	currentFrame = 1;
}
void AnimationLayer::ResetActionPrev()
{
	currentFramePrevAction = 1;
}
void AnimationLayer::PauseAction()
{
	playing = false;
}
void AnimationLayer::StopAction()
{
	ResetAction();
	PauseAction();
}
void AnimationLayer::PlayAction()
{
	playing = true;
}
void AnimationLayer::Serialize(wiArchive& archive)
{
	if (archive.IsReadMode())
	{
		archive >> name;
		archive >> blendFrames;
		archive >> blendFact;
		archive >> weight;
		int temp;
		archive >> temp;
		type = (ANIMATIONLAYER_TYPE)temp;
		archive >> looped;
	}
	else
	{
		archive << name;
		archive << blendFrames;
		archive << blendFact;
		archive << weight;
		archive << (int)type;
		archive << looped;
	}
}
#pragma endregion

#pragma region ARMATURE
Armature::~Armature()
{
	actions.clear();
	for (Bone* b : boneCollection)
	{
		SAFE_DELETE(b);
	}
	boneCollection.clear();
	rootbones.clear();
	for (auto& x : animationLayers)
	{
		SAFE_DELETE(x);
	}
	animationLayers.clear();
}
void Armature::UpdateTransform()
{
	Transform::UpdateTransform();

	// calculate frame
	for (Bone* root : rootbones) 
	{
		RecursiveBoneTransform(this, root, getMatrix());
	}

}
void Armature::UpdateArmature()
{
	for (auto& x : animationLayers)
	{
		if (x == nullptr)
			continue;

		AnimationLayer& anim = *x;

		// current action
		float cf = anim.currentFrame;
		int maxCf = 0;
		int activeAction = anim.activeAction;
		int prevAction = anim.prevAction;
		float frameInc = (anim.playing ? wiRenderer::GetGameSpeed() : 0.f);

		cf = anim.currentFrame += frameInc;
		maxCf = actions[activeAction].frameCount;
		if ((int)cf > maxCf)
		{
			if (anim.looped)
			{
				anim.ResetAction();
				cf = anim.currentFrame;
			}
			else
			{
				anim.currentFrame = cf = (float)maxCf;
			}
		}


		// prev action
		float cfPrevAction = anim.currentFramePrevAction;
		int maxCfPrevAction = actions[prevAction].frameCount;
		cfPrevAction = anim.currentFramePrevAction += frameInc;
		if ((int)cfPrevAction > maxCfPrevAction)
		{
			if (anim.looped)
			{
				anim.ResetActionPrev();
				cfPrevAction = anim.currentFramePrevAction;
			}
			else
			{
				anim.currentFramePrevAction = cfPrevAction = (float)maxCfPrevAction;
			}
		}

		// blending
		anim.blendCurrentFrame += frameInc;
		if (abs(anim.blendFrames) > 0)
			anim.blendFact = wiMath::Clamp(anim.blendCurrentFrame / anim.blendFrames, 0, 1);
		else
			anim.blendFact = 1;
	}
}
void Armature::RecursiveBoneTransform(Armature* armature, Bone* bone, const XMMATRIX& parentCombinedMat)
{
	Bone* parent = (Bone*)bone->parent;

	// TRANSITION BLENDING + ADDITIVE BLENDING
	XMVECTOR& finalTrans = XMVectorSet(0, 0, 0, 0);
	XMVECTOR& finalRotat = XMQuaternionIdentity();
	XMVECTOR& finalScala = XMVectorSet(1, 1, 1, 0);

	for (auto& x : armature->animationLayers)
	{
		AnimationLayer& anim = *x;

		float cf = anim.currentFrame, cfPrev = anim.currentFramePrevAction;
		int activeAction = anim.activeAction, prevAction = anim.prevAction;
		int maxCf = armature->actions[activeAction].frameCount, maxCfPrev = armature->actions[prevAction].frameCount;

		XMVECTOR& prevTrans = InterPolateKeyFrames(cfPrev, maxCfPrev, bone->actionFrames[prevAction].keyframesPos, POSITIONKEYFRAMETYPE);
		XMVECTOR& prevRotat = InterPolateKeyFrames(cfPrev, maxCfPrev, bone->actionFrames[prevAction].keyframesRot, ROTATIONKEYFRAMETYPE);
		XMVECTOR& prevScala = InterPolateKeyFrames(cfPrev, maxCfPrev, bone->actionFrames[prevAction].keyframesSca, SCALARKEYFRAMETYPE);

		XMVECTOR& currTrans = InterPolateKeyFrames(cf, maxCf, bone->actionFrames[activeAction].keyframesPos, POSITIONKEYFRAMETYPE);
		XMVECTOR& currRotat = InterPolateKeyFrames(cf, maxCf, bone->actionFrames[activeAction].keyframesRot, ROTATIONKEYFRAMETYPE);
		XMVECTOR& currScala = InterPolateKeyFrames(cf, maxCf, bone->actionFrames[activeAction].keyframesSca, SCALARKEYFRAMETYPE);

		float blendFact = anim.blendFact;

		switch (anim.type)
		{
		case AnimationLayer::ANIMLAYER_TYPE_PRIMARY:
			finalTrans = XMVectorLerp(prevTrans, currTrans, blendFact);
			finalRotat = XMQuaternionSlerp(prevRotat, currRotat, blendFact);
			finalScala = XMVectorLerp(prevScala, currScala, blendFact);
			break;
		case AnimationLayer::ANIMLAYER_TYPE_ADDITIVE:
			finalTrans = XMVectorLerp(finalTrans, XMVectorAdd(finalTrans, XMVectorLerp(prevTrans, currTrans, blendFact)), anim.weight);
			finalRotat = XMQuaternionSlerp(finalRotat, XMQuaternionMultiply(finalRotat, XMQuaternionSlerp(prevRotat, currRotat, blendFact)), anim.weight); // normalize?
			finalScala = XMVectorLerp(finalScala, XMVectorMultiply(finalScala, XMVectorLerp(prevScala, currScala, blendFact)), anim.weight);
			break;
		default:
			break;
		}
	}
	XMVectorSetW(finalTrans, 1);
	XMVectorSetW(finalScala, 1);

	bone->worldPrev = bone->world;
	bone->translationPrev = bone->translation;
	bone->rotationPrev = bone->rotation;
	XMStoreFloat3(&bone->translation, finalTrans);
	XMStoreFloat4(&bone->rotation, finalRotat);
	XMStoreFloat3(&bone->scale, finalScala);

	XMMATRIX& anim =
		XMMatrixScalingFromVector(finalScala)
		* XMMatrixRotationQuaternion(finalRotat)
		* XMMatrixTranslationFromVector(finalTrans);

	XMMATRIX& rest =
		XMLoadFloat4x4(&bone->world_rest);

	XMMATRIX& boneMat =
		anim * rest * parentCombinedMat
		;

	XMMATRIX& finalMat =
		XMLoadFloat4x4(&bone->recursiveRestInv)*
		boneMat
		;

	XMStoreFloat4x4(&bone->world, boneMat);

	bone->boneRelativityPrev = bone->boneRelativity;
	XMStoreFloat4x4(&bone->boneRelativity, finalMat);

	for (unsigned int i = 0; i<bone->childrenI.size(); ++i) {
		RecursiveBoneTransform(armature, bone->childrenI[i], boneMat);
	}

	// Because bones are not updated in the regular Transform fashion, a separate update needs to be called
	bone->UpdateTransform();
}
XMVECTOR Armature::InterPolateKeyFrames(float cf, const int maxCf, const vector<KeyFrame>& keyframeList, KeyFrameType type)
{
	XMVECTOR result = XMVectorSet(0, 0, 0, 0);

	if (type == POSITIONKEYFRAMETYPE) result = XMVectorSet(0, 0, 0, 1);
	if (type == ROTATIONKEYFRAMETYPE) result = XMVectorSet(0, 0, 0, 1);
	if (type == SCALARKEYFRAMETYPE)   result = XMVectorSet(1, 1, 1, 1);

	//SEARCH 2 INTERPOLATABLE FRAMES
	int nearest[2] = { 0,0 };
	int first = 0, last = 0;
	if (keyframeList.size()>1) {
		first = keyframeList[0].frameI;
		last = keyframeList.back().frameI;

		if (cf <= first) { //BROKEN INTERVAL
			nearest[0] = 0;
			nearest[1] = 0;
		}
		else if (cf >= last) {
			nearest[0] = (int)(keyframeList.size() - 1);
			nearest[1] = (int)(keyframeList.size() - 1);
		}
		else { //IN BETWEEN TWO KEYFRAMES, DECIDE WHICH
			for (int k = (int)keyframeList.size() - 1; k>0; k--)
				if (keyframeList[k].frameI <= cf) {
					nearest[0] = k;
					break;
				}
			for (int k = 0; k<(int)keyframeList.size(); k++)
				if (keyframeList[k].frameI >= cf) {
					nearest[1] = k;
					break;
				}
		}

		//INTERPOLATE BETWEEN THE TWO FRAMES
		int keyframes[2] = {
			keyframeList[nearest[0]].frameI,
			keyframeList[nearest[1]].frameI
		};
		float interframe = 0;
		if (cf <= first || cf >= last) { //BROKEN INTERVAL
			float intervalBegin = (float)(maxCf - keyframes[0]);
			float intervalEnd = keyframes[1] + intervalBegin;
			float intervalLen = abs(intervalEnd - intervalBegin);
			float offsetCf = cf + intervalBegin;
			if (intervalLen) interframe = offsetCf / intervalLen;
		}
		else {
			float intervalBegin = (float)keyframes[0];
			float intervalEnd = (float)keyframes[1];
			float intervalLen = abs(intervalEnd - intervalBegin);
			float offsetCf = cf - intervalBegin;
			if (intervalLen) interframe = offsetCf / intervalLen;
		}

		if (type == ROTATIONKEYFRAMETYPE) {
			XMVECTOR quat[2] = {
				XMLoadFloat4(&keyframeList[nearest[0]].data),
				XMLoadFloat4(&keyframeList[nearest[1]].data)
			};
			result = XMQuaternionNormalize(XMQuaternionSlerp(quat[0], quat[1], interframe));
		}
		else {
			XMVECTOR tran[2] = {
				XMLoadFloat4(&keyframeList[nearest[0]].data),
				XMLoadFloat4(&keyframeList[nearest[1]].data)
			};
			result = XMVectorLerp(tran[0], tran[1], interframe);
		}
	}
	else {
		if (!keyframeList.empty())
			result = XMLoadFloat4(&keyframeList.back().data);
	}

	return result;
}

void Armature::ChangeAction(const string& actionName, float blendFrames, const string& animLayer, float weight)
{
	AnimationLayer* anim = GetPrimaryAnimation();
	if (animLayer.length() > 0)
	{
		anim = GetAnimLayer(animLayer);
	}

	if (actionName.length() > 0)
	{
		for (unsigned int i = 1; i < actions.size(); ++i)
		{
			if (!actions[i].name.compare(actionName))
			{
				anim->ChangeAction(i, blendFrames, weight);
				return;
			}
		}
	}

	// Fall back to identity action
	anim->ChangeAction(0, blendFrames, weight);
}
AnimationLayer* Armature::GetAnimLayer(const string& name)
{
	for (auto& x : animationLayers)
	{
		if (!x->name.compare(name))
		{
			return x;
		}
	}
	animationLayers.push_back(new AnimationLayer);
	animationLayers.back()->name = name;
	return animationLayers.back();
}
void Armature::AddAnimLayer(const string& name)
{
	for (auto& x : animationLayers)
	{
		if (!x->name.compare(name))
		{
			return;
		}
	}
	animationLayers.push_back(new AnimationLayer);
	animationLayers.back()->name = name;
}
void Armature::DeleteAnimLayer(const string& name)
{
	auto i = animationLayers.begin();
	while (i != animationLayers.end())
	{
		if ((*i)->type != AnimationLayer::ANIMLAYER_TYPE_PRIMARY && !(*i)->name.compare(name))
		{
			animationLayers.erase(i++);
		}
		else
		{
			++i;
		}
	}
}
void Armature::CreateFamily()
{
	for (Bone* i : boneCollection) {
		if (i->parentName.length()>0) {
			for (Bone* j : boneCollection) {
				if (i != j) {
					if (!i->parentName.compare(j->name)) {
						i->parent = j;
						j->childrenN.push_back(i->name);
						j->childrenI.push_back(i);
						i->attachTo(j, 1, 1, 1);
					}
				}
			}
		}
		else {
			rootbones.push_back(i);
		}
	}

	for (unsigned int i = 0; i<rootbones.size(); ++i) {
		RecursiveRest(this, rootbones[i]);
	}
}
Bone* Armature::GetBone(const string& name)
{
	for (auto& x : boneCollection)
	{
		if (!x->name.compare(name))
		{
			return x;
		}
	}
	return nullptr;
}
void Armature::Serialize(wiArchive& archive)
{
	Transform::Serialize(archive);

	if (archive.IsReadMode())
	{
		archive >> unidentified_name;
		size_t boneCount;
		archive >> boneCount;
		for (size_t i = 0; i < boneCount; ++i)
		{
			Bone* bone = new Bone;
			bone->Serialize(archive);
			boneCollection.push_back(bone);
		}
		size_t animLayerCount;
		archive >> animLayerCount;
		animationLayers.clear();
		for (size_t i = 0; i < animLayerCount; ++i)
		{
			AnimationLayer* layer = new AnimationLayer;
			layer->Serialize(archive);
			animationLayers.push_back(layer);
		}
		size_t actionCount;
		archive >> actionCount;
		Action tempAction;
		actions.clear();
		for (size_t i = 0; i < actionCount; ++i)
		{
			archive >> tempAction.name;
			archive >> tempAction.frameCount;
			actions.push_back(tempAction);
		}

		CreateFamily();
	}
	else
	{
		archive << unidentified_name;
		archive << boneCollection.size();
		for (auto& x : boneCollection)
		{
			x->Serialize(archive);
		}
		archive << animationLayers.size();
		for (auto& x : animationLayers)
		{
			x->Serialize(archive);
		}
		archive << actions.size();
		for (auto& x : actions)
		{
			archive << x.name;
			archive << x.frameCount;
		}
	}
}
#pragma endregion

#pragma region Decals
Decal::Decal(const XMFLOAT3& tra, const XMFLOAT3& sca, const XMFLOAT4& rot, const string& tex, const string& nor):Cullable(),Transform(){
	scale_rest=scale=sca;
	rotation_rest=rotation=rot;
	translation_rest=translation=tra;

	UpdateTransform();

	texture=normal=nullptr;
	addTexture(tex);
	addNormal(nor);

	life = -2; //persistent
	fadeStart=0;
}
Decal::~Decal() {
	wiResourceManager::GetGlobal()->del(texName);
	wiResourceManager::GetGlobal()->del(norName);
}
void Decal::addTexture(const string& tex){
	texName=tex;
	if(!tex.empty()){
		texture = (Texture2D*)wiResourceManager::GetGlobal()->add(tex);
	}
}
void Decal::addNormal(const string& nor){
	norName=nor;
	if(!nor.empty()){
		normal = (Texture2D*)wiResourceManager::GetGlobal()->add(nor);
	}
}
void Decal::UpdateTransform()
{
	Transform::UpdateTransform();

	XMMATRIX rotMat = XMMatrixRotationQuaternion(XMLoadFloat4(&rotation));
	XMVECTOR eye = XMLoadFloat3(&translation);
	XMStoreFloat4x4(&world_rest, XMMatrixScalingFromVector(XMLoadFloat3(&scale))*rotMat*XMMatrixTranslationFromVector(eye));

	bounds.createFromHalfWidth(XMFLOAT3(0, 0, 0), XMFLOAT3(scale.x*0.5f, scale.y*0.5f, scale.z*0.5f));
	bounds = bounds.get(XMLoadFloat4x4(&world_rest));

}
void Decal::UpdateDecal()
{
	if (life>-2) {
		life -= wiRenderer::GetGameSpeed();
	}
}
void Decal::Serialize(wiArchive& archive)
{
	Cullable::Serialize(archive);
	Transform::Serialize(archive);

	if (archive.IsReadMode())
	{
		archive >> texName;
		archive >> norName;
		archive >> life;
		archive >> fadeStart;

		string texturesDir = archive.GetSourceDirectory() + "textures/";
		if (!texName.empty())
		{
			texName = texturesDir + texName;
			texture = (Texture2D*)wiResourceManager::GetGlobal()->add(texName);
		}
		if (!norName.empty())
		{
			norName = texturesDir + norName;
			normal = (Texture2D*)wiResourceManager::GetGlobal()->add(norName);
		}

	}
	else
	{
		archive << wiHelper::GetFileNameFromPath(texName);
		archive << wiHelper::GetFileNameFromPath(norName);
		archive << life;
		archive << fadeStart;
	}
}
#pragma endregion

#pragma region CAMERA
void Camera::UpdateTransform()
{
	Transform::UpdateTransform();

	//getMatrix();
	UpdateProps();
}
#pragma endregion

#pragma region OBJECT
Object::Object(const string& name) :Transform()
{
	this->name = name;
	init();

	GPUQueryDesc desc;
	desc.Type = GPU_QUERY_TYPE_OCCLUSION_PREDICATE;
	desc.MiscFlags = 0;
	for (int i = 0; i < ARRAYSIZE(occlusionQueries); ++i)
	{
		wiRenderer::GetDevice()->CreateQuery(&desc, &occlusionQueries[i]);
		occlusionQueries[i].result_passed = TRUE;
	}
}
Object::~Object() {
}
void Object::EmitTrail(const XMFLOAT3& col, float fadeSpeed) {
	if (mesh != nullptr)
	{
		int base = mesh->trailInfo.base;
		int tip = mesh->trailInfo.tip;


		//int x = trail.size();

		if (base >= 0 && tip >= 0) {
			XMFLOAT4 baseP, tipP;
			XMFLOAT4 newCol = XMFLOAT4(col.x, col.y, col.z, 1);
			baseP = wiRenderer::TransformVertex(mesh, base).pos;
			tipP = wiRenderer::TransformVertex(mesh, tip).pos;

			trail.push_back(RibbonVertex(XMFLOAT3(baseP.x, baseP.y, baseP.z), XMFLOAT2(0,0), XMFLOAT4(0, 0, 0, 1),fadeSpeed));
			trail.push_back(RibbonVertex(XMFLOAT3(tipP.x, tipP.y, tipP.z), XMFLOAT2(0,0), newCol,fadeSpeed));
		}
	}
}
void Object::FadeTrail() {
	for (unsigned int j = 0; j<trail.size(); ++j) {
		const float fade = trail[j].fade;
		if (trail[j].col.x>0) trail[j].col.x = trail[j].col.x - fade*(wiRenderer::GetGameSpeed() + wiRenderer::GetGameSpeed()*(1 - j % 2) * 2);
		else trail[j].col.x = 0;
		if (trail[j].col.y>0) trail[j].col.y = trail[j].col.y - fade*(wiRenderer::GetGameSpeed() + wiRenderer::GetGameSpeed()*(1 - j % 2) * 2);
		else trail[j].col.y = 0;
		if (trail[j].col.z>0) trail[j].col.z = trail[j].col.z - fade*(wiRenderer::GetGameSpeed() + wiRenderer::GetGameSpeed()*(1 - j % 2) * 2);
		else trail[j].col.z = 0;
		if (trail[j].col.w>0)
			trail[j].col.w -= fade*wiRenderer::GetGameSpeed();
		else
			trail[j].col.w = 0;

#if 0
		// Collapse trail... perhaps will be needed
		if (j % 2 == 0)
		{
			trail[j].pos = wiMath::Lerp(trail[j].pos, trail[j + 1].pos, trail[j].fade * wiRenderer::GetGameSpeed());
			trail[j + 1].pos = wiMath::Lerp(trail[j + 1].pos, trail[j].pos, trail[j + 1].fade * wiRenderer::GetGameSpeed());
		}
#endif
	}
	while (!trail.empty() && trail.front().col.w <= 0)
	{
		trail.pop_front();
	}
}
void Object::UpdateTransform()
{
	Transform::UpdateTransform();

}
void Object::UpdateObject()
{
	XMMATRIX world = getMatrix();

	if (mesh->isBillboarded) {
		XMMATRIX bbMat = XMMatrixIdentity();
		if (mesh->billboardAxis.x || mesh->billboardAxis.y || mesh->billboardAxis.z) {
			float angle = 0;
			angle = (float)atan2(translation.x - wiRenderer::getCamera()->translation.x, translation.z - wiRenderer::getCamera()->translation.z) * (180.0f / XM_PI);
			bbMat = XMMatrixRotationAxis(XMLoadFloat3(&mesh->billboardAxis), angle * 0.0174532925f);
		}
		else
			bbMat = XMMatrixInverse(0, XMMatrixLookAtLH(XMVectorSet(0, 0, 0, 0), XMVectorSubtract(XMLoadFloat3(&translation), wiRenderer::getCamera()->GetEye()), XMVectorSet(0, 1, 0, 0)));

		XMMATRIX w = XMMatrixScalingFromVector(XMLoadFloat3(&scale)) *
			bbMat *
			XMMatrixRotationQuaternion(XMLoadFloat4(&rotation)) *
			XMMatrixTranslationFromVector(XMLoadFloat3(&translation)
				);
		XMStoreFloat4x4(&this->world, w);
	}

	if (mesh->softBody)
		bounds = mesh->aabb;
	else if (!mesh->isBillboarded && mesh->renderable) {
		bounds = mesh->aabb.get(world);
	}
	else if (mesh->renderable)
		bounds.createFromHalfWidth(translation, scale);

	if (!trail.empty())
	{
		wiRenderer::objectsWithTrails.push_back(this);
		FadeTrail();
	}

	for (wiEmittedParticle* x : eParticleSystems)
	{
		x->Update(wiRenderer::GetGameSpeed());
		wiRenderer::emitterSystems.push_back(x);
	}
}
bool Object::IsCastingShadow() const
{
	for (auto& x : mesh->subsets)
	{
		if (x.material->IsCastingShadow())
		{
			return true;
		}
	}
	return false;
}
bool Object::IsReflector() const
{
	for (auto& x : mesh->subsets)
	{
		if (x.material->HasPlanarReflection())
		{
			return true;
		}
	}
	return false;
}
int Object::GetRenderTypes() const
{
	int retVal = RENDERTYPE::RENDERTYPE_VOID;
	for (auto& x : mesh->subsets)
	{
		retVal |= x.material->GetRenderType();
	}
	return retVal;
}
void Object::Serialize(wiArchive& archive)
{
	Streamable::Serialize(archive);
	Transform::Serialize(archive);

	if (archive.IsReadMode())
	{
		int temp;
		archive >> temp;
		emitterType = (EmitterType)temp;
		archive >> transparency;
		archive >> color;
		archive >> rigidBody;
		archive >> kinematic;
		archive >> collisionShape;
		archive >> physicsType;
		archive >> mass;
		archive >> friction;
		archive >> restitution;
		archive >> damping;

		size_t emitterPSCount;
		archive >> emitterPSCount;
		for (size_t i = 0; i < emitterPSCount; ++i)
		{
			wiEmittedParticle* e = new wiEmittedParticle;
			e->Serialize(archive);
			eParticleSystems.push_back(e);
		}
		size_t hairPSCount;
		archive >> hairPSCount;
		for (size_t i = 0; i < hairPSCount; ++i)
		{
			wiHairParticle* h = new wiHairParticle;
			h->Serialize(archive);
			hParticleSystems.push_back(h);
		}
	}
	else
	{
		archive << (int)emitterType;
		archive << transparency;
		archive << color;
		archive << rigidBody;
		archive << kinematic;
		archive << collisionShape;
		archive << physicsType;
		archive << mass;
		archive << friction;
		archive << restitution;
		archive << damping;

		archive << eParticleSystems.size();
		for (auto& x : eParticleSystems)
		{
			x->Serialize(archive);
		}
		archive << hParticleSystems.size();
		for (auto& x : hParticleSystems)
		{
			x->Serialize(archive);
		}
	}
}
#pragma endregion

#pragma region LIGHT
Texture2D* Light::shadowMapArray_2D = nullptr;
Texture2D* Light::shadowMapArray_Cube = nullptr;
Light::Light():Transform() {
	color = XMFLOAT4(0, 0, 0, 0);
	enerDis = XMFLOAT4(0, 0, 0, 0);
	type = LightType::POINT;
	shadow = false;
	noHalo = false;
	lensFlareRimTextures.resize(0);
	lensFlareNames.resize(0);
	shadowMap_index = -1;
	lightArray_index = 0;
	shadowBias = 0.0001f;
}
Light::~Light() {
	for (string x : lensFlareNames)
		wiResourceManager::GetGlobal()->del(x);
}
XMFLOAT3 Light::GetDirection()
{
	XMMATRIX rot = XMMatrixRotationQuaternion(XMLoadFloat4(&rotation));
	XMFLOAT3 retVal;
	XMStoreFloat3(&retVal, XMVector3Normalize(-XMVector3Transform(XMVectorSet(0, -1, 0, 1), rot)));
	return retVal;
}
void Light::UpdateTransform()
{
	Transform::UpdateTransform();
}
void Light::UpdateLight()
{
	//Shadows
	if (type == Light::DIRECTIONAL) 
	{
		if (shadow)
		{
			if (shadowCam_dirLight.empty())
			{
				float lerp = 0.5f;
				float lerp1 = 0.12f;
				float lerp2 = 0.016f;
				XMVECTOR a0, a, b0, b;
				a0 = XMVector3Unproject(XMVectorSet(0, (float)wiRenderer::GetDevice()->GetScreenHeight(), 0, 1), 0, 0, (float)wiRenderer::GetDevice()->GetScreenWidth(), (float)wiRenderer::GetDevice()->GetScreenHeight(), 0.1f, 1.0f, wiRenderer::getCamera()->GetProjection(), wiRenderer::getCamera()->GetView(), XMMatrixIdentity());
				a = XMVector3Unproject(XMVectorSet(0, (float)wiRenderer::GetDevice()->GetScreenHeight(), 1, 1), 0, 0, (float)wiRenderer::GetDevice()->GetScreenWidth(), (float)wiRenderer::GetDevice()->GetScreenHeight(), 0.1f, 1.0f, wiRenderer::getCamera()->GetProjection(), wiRenderer::getCamera()->GetView(), XMMatrixIdentity());
				b0 = XMVector3Unproject(XMVectorSet((float)wiRenderer::GetDevice()->GetScreenWidth(), (float)wiRenderer::GetDevice()->GetScreenHeight(), 0, 1), 0, 0, (float)wiRenderer::GetDevice()->GetScreenWidth(), (float)wiRenderer::GetDevice()->GetScreenHeight(), 0.1f, 1.0f, wiRenderer::getCamera()->GetProjection(), wiRenderer::getCamera()->GetView(), XMMatrixIdentity());
				b = XMVector3Unproject(XMVectorSet((float)wiRenderer::GetDevice()->GetScreenWidth(), (float)wiRenderer::GetDevice()->GetScreenHeight(), 1, 1), 0, 0, (float)wiRenderer::GetDevice()->GetScreenWidth(), (float)wiRenderer::GetDevice()->GetScreenHeight(), 0.1f, 1.0f, wiRenderer::getCamera()->GetProjection(), wiRenderer::getCamera()->GetView(), XMMatrixIdentity());
				float size = XMVectorGetX(XMVector3Length(XMVectorSubtract(XMVectorLerp(b0, b, lerp), XMVectorLerp(a0, a, lerp))));
				float size1 = XMVectorGetX(XMVector3Length(XMVectorSubtract(XMVectorLerp(b0, b, lerp1), XMVectorLerp(a0, a, lerp1))));
				float size2 = XMVectorGetX(XMVector3Length(XMVectorSubtract(XMVectorLerp(b0, b, lerp2), XMVectorLerp(a0, a, lerp2))));
				XMVECTOR rot = XMQuaternionIdentity();

				shadowCam_dirLight.push_back(SHCAM(size, rot, 0, wiRenderer::getCamera()->zFarP));
				shadowCam_dirLight.push_back(SHCAM(size1, rot, 0, wiRenderer::getCamera()->zFarP));
				shadowCam_dirLight.push_back(SHCAM(size2, rot, 0, wiRenderer::getCamera()->zFarP));
			}

			float lerp = 0.5f;//third slice distance from cam (percentage)
			float lerp1 = 0.12f;//second slice distance from cam (percentage)
			float lerp2 = 0.016f;//first slice distance from cam (percentage)
			XMVECTOR c, d, e, e1, e2;
			c = XMVector3Unproject(XMVectorSet((float)wiRenderer::GetDevice()->GetScreenWidth() * 0.5f, (float)wiRenderer::GetDevice()->GetScreenHeight() * 0.5f, 1, 1), 0, 0, (float)wiRenderer::GetDevice()->GetScreenWidth(), (float)wiRenderer::GetDevice()->GetScreenHeight(), 0.1f, 1.0f, wiRenderer::getCamera()->GetProjection(), wiRenderer::getCamera()->GetView(), XMMatrixIdentity());
			d = XMVector3Unproject(XMVectorSet((float)wiRenderer::GetDevice()->GetScreenWidth() * 0.5f, (float)wiRenderer::GetDevice()->GetScreenHeight() * 0.5f, 0, 1), 0, 0, (float)wiRenderer::GetDevice()->GetScreenWidth(), (float)wiRenderer::GetDevice()->GetScreenHeight(), 0.1f, 1.0f, wiRenderer::getCamera()->GetProjection(), wiRenderer::getCamera()->GetView(), XMMatrixIdentity());

			if (!shadowCam_dirLight.empty()) {

				float f = shadowCam_dirLight[0].size / (float)wiRenderer::SHADOWRES_2D;
				e = XMVectorFloor(XMVectorLerp(d, c, lerp) / f)*f;
				f = shadowCam_dirLight[1].size / (float)wiRenderer::SHADOWRES_2D;
				e1 = XMVectorFloor(XMVectorLerp(d, c, lerp1) / f)*f;
				f = shadowCam_dirLight[2].size / (float)wiRenderer::SHADOWRES_2D;
				e2 = XMVectorFloor(XMVectorLerp(d, c, lerp2) / f)*f;

				XMMATRIX rrr = XMMatrixRotationQuaternion(XMLoadFloat4(&rotation));
				shadowCam_dirLight[0].Update(rrr*XMMatrixTranslationFromVector(e));
				if (shadowCam_dirLight.size()>1) {
					shadowCam_dirLight[1].Update(rrr*XMMatrixTranslationFromVector(e1));
					if (shadowCam_dirLight.size()>2)
						shadowCam_dirLight[2].Update(rrr*XMMatrixTranslationFromVector(e2));
				}
			}
		}


		bounds.createFromHalfWidth(wiRenderer::getCamera()->translation, XMFLOAT3(10000, 10000, 10000));
	}
	else if (type == Light::SPOT) {
		if (shadow)
		{
			if (shadowCam_spotLight.empty()) 
			{
				shadowCam_spotLight.push_back(SHCAM(XMFLOAT4(0, 0, 0, 1), 0.1f, 1000.0f, enerDis.z));
			}
			shadowCam_spotLight[0].Update(XMLoadFloat4x4(&world));
			shadowCam_spotLight[0].farplane = enerDis.y;
			shadowCam_spotLight[0].Create_Perspective(enerDis.z);
		}

		bounds.createFromHalfWidth(translation, XMFLOAT3(enerDis.y, enerDis.y, enerDis.y));
	}
	else if (type == Light::POINT) {
		if (shadow)
		{
			if (shadowCam_pointLight.empty())
			{
				shadowCam_pointLight.push_back(SHCAM(XMFLOAT4(0.5f, -0.5f, -0.5f, -0.5f), 0.1f, 1000.0f, XM_PIDIV2)); //+x
				shadowCam_pointLight.push_back(SHCAM(XMFLOAT4(0.5f, 0.5f, 0.5f, -0.5f), 0.1f, 1000.0f, XM_PIDIV2)); //-x

				shadowCam_pointLight.push_back(SHCAM(XMFLOAT4(1, 0, 0, -0), 0.1f, 1000.0f, XM_PIDIV2)); //+y
				shadowCam_pointLight.push_back(SHCAM(XMFLOAT4(0, 0, 0, -1), 0.1f, 1000.0f, XM_PIDIV2)); //-y

				shadowCam_pointLight.push_back(SHCAM(XMFLOAT4(0.707f, 0, 0, -0.707f), 0.1f, 1000.0f, XM_PIDIV2)); //+z
				shadowCam_pointLight.push_back(SHCAM(XMFLOAT4(0, 0.707f, 0.707f, 0), 0.1f, 1000.0f, XM_PIDIV2)); //-z
			}
			for (unsigned int i = 0; i < shadowCam_pointLight.size(); ++i) {
				shadowCam_pointLight[i].Update(XMLoadFloat3(&translation));
				shadowCam_pointLight[i].farplane = enerDis.y;
				shadowCam_pointLight[i].Create_Perspective(XM_PIDIV2);
			}
		}

		bounds.createFromHalfWidth(translation, XMFLOAT3(enerDis.y, enerDis.y, enerDis.y));
	}
}
void Light::Serialize(wiArchive& archive)
{
	Cullable::Serialize(archive);
	Transform::Serialize(archive);

	if (archive.IsReadMode())
	{
		archive >> color;
		archive >> enerDis;
		archive >> noHalo;
		archive >> shadow;
		archive >> shadowBias;
		int temp;
		archive >> temp;
		type = (LightType)temp;
		//if (type == DIRECTIONAL)
		//{
		//	shadowMaps_dirLight.resize(3);
		//}
		size_t lensFlareCount;
		archive >> lensFlareCount;
		string rim;
		for (size_t i = 0; i < lensFlareCount; ++i)
		{
			archive >> rim;
			Texture2D* tex;
			rim = archive.GetSourceDirectory() + "rims/" + rim;
			if (!rim.empty() && (tex = (Texture2D*)wiResourceManager::GetGlobal()->add(rim)) != nullptr) {
				lensFlareRimTextures.push_back(tex);
				lensFlareNames.push_back(rim);
			}
		}
	}
	else
	{
		archive << color;
		archive << enerDis;
		archive << noHalo;
		archive << shadow;
		archive << shadowBias;
		archive << (int)type;
		archive << lensFlareNames.size();
		for (auto& x : lensFlareNames)
		{
			archive << wiHelper::GetFileNameFromPath(x);
		}
	}
}
#pragma endregion
