/*=========================================================================

 Program: FEMUS
 Module: Mesh
 Authors: Eugenio Aulisa

 Copyright (c) FEMTTU
 All rights reserved.

 This software is distributed WITHOUT ANY WARRANTY; without even
 the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

//----------------------------------------------------------------------------
// includes :
//----------------------------------------------------------------------------
#include "Mesh.hpp"
#include "MeshGeneration.hpp"
#include "GambitIO.hpp"
#include "MED_IO.hpp"
#include "MeshMetisPartitioning.hpp"
#include "NumericVector.hpp"

// C++ includes
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <algorithm>


namespace femus {

  using std::cout;
  using std::endl;
  using std::min;
  using std::sort;
  using std::map;

  bool Mesh::_IsUserRefinementFunctionDefined = false;

  unsigned Mesh::_dimension = 2;
  unsigned Mesh::_ref_index = 4; // 8*DIM[2]+4*DIM[1]+2*DIM[0];
  unsigned Mesh::_face_index = 2; // 4*DIM[2]+2*DIM[1]+1*DIM[0];

//------------------------------------------------------------------------------------------------------
  Mesh::Mesh() {

    _coarseMsh = NULL;

    for(int i = 0; i < 5; i++) {
      _ProjCoarseToFine[i] = NULL;
    }

    for(int itype = 0; itype < 3; itype++) {
      for(int jtype = 0; jtype < 3; jtype++) {
        _ProjQitoQj[itype][jtype] = NULL;
      }
    }
  }


  Mesh::~Mesh() {
      
    delete el;
    
    _topology->FreeSolutionVectors();
    delete _topology;

    for(int itype = 0; itype < 3; itype++) {
      for(int jtype = 0; jtype < 3; jtype++) {
        if(_ProjQitoQj[itype][jtype]) {
          delete _ProjQitoQj[itype][jtype];
          _ProjQitoQj[itype][jtype] = NULL;
        }
      }
    }

    for(unsigned i = 0; i < 5; i++) {
      if(_ProjCoarseToFine[i]) {
        delete _ProjCoarseToFine[i];
        _ProjCoarseToFine[i] = NULL;
      }
    }
    
  }
  
  

/// print Mesh info
  void Mesh::PrintInfo() const {

      PrintInfoLevel();
      PrintInfoElements();
      PrintInfoNodes();
      std::cout << std::endl;

  }
  
  
  void Mesh::PrintInfoLevel() const {
    std::cout << " Mesh Level                  : " << _level  << std::endl;
  }
  
  
  void Mesh::PrintInfoElements() const {
    std::cout << " Number of elements          : " << _nelem  << std::endl;
  }
  
  void Mesh::PrintInfoNodes() const {
    std::cout << " Number of linear nodes      : " << _dofOffset[0][_nprocs] << std::endl;
    std::cout << " Number of quadratic nodes   : " << _dofOffset[1][_nprocs] << std::endl;
    std::cout << " Number of biquadratic nodes : " << _dofOffset[2][_nprocs] << std::endl;
  }
  
  
  
  const unsigned Mesh::_numberOfMissedBiquadraticNodes[N_GEOM_ELS] = {0, 5, 3, 0, 1, 0};
  const double Mesh::_baricentricWeight[N_GEOM_ELS][5][18] = {
    {},
    {
      { -1. / 9., -1. / 9., -1. / 9.,  0    , 4. / 9., 4. / 9., 4. / 9., 0.   , 0.   , 0.   },
      { -1. / 9., -1. / 9.,  0.   , -1. / 9., 4. / 9., 0.   , 0.   , 4. / 9., 4. / 9., 0.   },
      {  0.   , -1. / 9., -1. / 9., -1. / 9., 0.   , 4. / 9., 0.   , 0.   , 4. / 9., 4. / 9.},
      { -1. / 9.,  0.    , -1. / 9., -1. / 9., 0.   , 0.   , 4. / 9., 4. / 9., 0.   , 4. / 9.},
      { -1. / 8., -1. / 8., -1. / 8., -1. / 8., 1. / 4., 1. / 4., 1. / 4., 1. / 4., 1. / 4., 1. / 4.}
    },
    {
      { -1. / 9., -1. / 9., -1. / 9., 0.   ,  0.   ,  0.   , 4. / 9., 4. / 9., 4. / 9.},
      {  0.   ,  0.   ,  0.   , -1. / 9., -1. / 9., -1. / 9., 0.   , 0.   , 0.   , 4. / 9., 4. / 9., 4. / 9.},
      {
        0.   ,  0.   ,  0.   , 0.   ,  0.   ,  0.   , 0.   , 0.   , 0.   , 0.   , 0.   , 0.   ,
        -1. / 9., -1. / 9., -1. / 9., 4. / 9.,  4. / 9.,  4. / 9.
      },
    },
    {},
    {{ -1. / 9., -1. / 9., -1. / 9., 4. / 9., 4. / 9., 4. / 9.}}
  };



  std::vector < unsigned >  Mesh::PartitionForElements() {

    std::vector < unsigned > partition;
    
    const bool flag_for_ncommon_in_metis = false;

    partition.resize(GetNumberOfElements());
    
    MeshMetisPartitioning meshMetisPartitioning(*this);
    
    meshMetisPartitioning.DoPartition(partition, flag_for_ncommon_in_metis);

    return partition;
    
  }
  
  
  void Mesh::Partition() {

    std::vector < unsigned > partition = PartitionForElements();
    
    FillISvector(partition);
    
    std::vector<unsigned> ().swap(partition);

  }


  /**
   *  This function generates the coarse Mesh level, $l_0$, from an input Mesh file
   **/
  void Mesh::ReadCoarseMesh(const std::string& name, const double Lref, std::vector<bool>& type_elem_flag) {


    const bool read_groups = true; //by default groups are read
    const bool read_boundary_groups = true; //by default boundary groups are read

    ReadCoarseMesh(name, Lref, type_elem_flag, read_groups, read_boundary_groups);

  }


  void Mesh::ReadCoarseMeshFile(const std::string& name, const double Lref, std::vector<bool>& type_elem_flag, const bool read_groups, const bool read_boundary_groups) {


    if(name.rfind(".neu") < name.size()) {
      GambitIO(*this).read(name, _coords, Lref, type_elem_flag, read_groups, read_boundary_groups);
    }

    // else if (name.rfind (".obj") < name.size()) {
    //   obj_io (*this).read (name, _coords, Lref, type_elem_flag);
    // }

#ifdef HAVE_HDF5
    else if(name.rfind(".med") < name.size()) {
      MED_IO(*this).read(name, _coords, Lref, type_elem_flag, read_groups, read_boundary_groups);
    }
#endif
    else {
      std::cerr << " ERROR: Unrecognized file extension: " << name
                << "\n   I understand the following:\n\n"
                << "     *.neu -- Gambit Neutral File\n"
                << "     *.med -- MED File\n"
                << std::endl;
      abort();
    }

  }
  
  

  void Mesh::ReadCoarseMeshBeforePartitioning(const std::string& name, const double Lref, std::vector<bool> & type_elem_flag, const bool read_groups, const bool read_boundary_groups) {

    SetIfHomogeneous(true);

    SetLevel(0);

    _coords.resize(3);


    ReadCoarseMeshFile(name, Lref, type_elem_flag, read_groups, read_boundary_groups);



    AddBiquadraticNodesNotInMeshFile();

    el->ShrinkToFit();

    //el->SetNodeNumber(_nnodes);

  }  
  

  /**
   *  This function generates the coarse Mesh level, $l_0$, from an input Mesh file
   **/
  void Mesh::ReadCoarseMesh(const std::string& name, const double Lref, std::vector<bool>& type_elem_flag, const bool read_groups, const bool read_boundary_groups) {
      

    ReadCoarseMeshBeforePartitioning(name, Lref, type_elem_flag, read_groups, read_boundary_groups);
    
    Partition();

    BuildElementAndNodeStructures();
   
  }
  
  

  void Mesh::BuildElementAndNodeStructures() {
      
    GetMeshElements()->BuildMeshElemStructures();  //must stay here, cannot be anticipated. Does it need dofmap already? I don't think so, but it needs the elem reordering and maybe also the node reordering
    
    BuildTopologyStructures();  //needs dofmap

    ComputeCharacteristicLength();  //doesn't need dofmap

    PrintInfo();  //needs dofmap

  }
  
  
  
  void Mesh::BuildTopologyStructures() {
      

    Topology_InitializeAndFillCoordinates();
    Topology_InitializeAMR();
    Topology_InitializeAndFillSolidNodeFlag();

    _amrRestriction.resize(3); /* 3 Lagrange continuous families (linear, quadr, biquadr) */


  }


 /// this needs all the dof maps, for the continuous Lagrange elements
  void Mesh::Topology_InitializeAndFillCoordinates() {
      
    _topology = new Solution(this);

    _topology->AddSolution("X", LAGRANGE, SECOND, 1, 0);
    _topology->AddSolution("Y", LAGRANGE, SECOND, 1, 0);
    _topology->AddSolution("Z", LAGRANGE, SECOND, 1, 0);

    _topology->ResizeSolutionVector("X");  //needs dofmap
    _topology->ResizeSolutionVector("Y");  //needs dofmap
    _topology->ResizeSolutionVector("Z");  //needs dofmap

    //set coordinates -----------
    _topology->GetSolutionName("X") = _coords[0];
    _topology->GetSolutionName("Y") = _coords[1];
    _topology->GetSolutionName("Z") = _coords[2];
    //set coordinates -----------
    

  }
  
  
/// This needs the dof maps, for the discontinuous Lagrange elements
  void Mesh::Topology_InitializeAMR() {
      
    _topology->AddSolution("AMR", DISCONTINUOUS_POLYNOMIAL, ZERO, 1, 0);

    _topology->ResizeSolutionVector("AMR");

  }
  

/// This needs the dof maps, for the continuous Lagrange elements
  void Mesh::Topology_InitializeAndFillSolidNodeFlag() {
      
    _topology->AddSolution("solidMrk", LAGRANGE, SECOND, 1, 0);
    
    AllocateAndMarkStructureNode();

  }
  

  
  void Mesh::ComputeCharacteristicLength() {
      
    //compute max and min coords -----------
    std::vector < double > xMax(3, 0.);
    std::vector < double > xMin(3, 0.);
    for(unsigned i = 0; i < _coords[0].size(); i++) {
      for(unsigned k = 0; k < 3; k++) {
        if(xMax[k] < _coords[k][i]) xMax[k] = _coords[k][i];
        if(xMin[k] > _coords[k][i]) xMin[k] = _coords[k][i];
      }
    }
    
    _cLength = sqrt(pow(xMax[0] - xMin[0], 2) + pow(xMax[1] - xMin[1], 2) + pow(xMax[2] - xMin[2], 2));
    //compute max and min coords - end -----------
    
  }
  
  
  /**
   *  This function generates the coarse Box Mesh level using the built-in generator
   *   ///@todo seems like GenerateCoarseBoxMesh doesn't assign flags to faces correctly, need to check that
   **/
  void Mesh::GenerateCoarseBoxMesh(
    const unsigned int nx, const unsigned int ny, const unsigned int nz,
    const double xmin, const double xmax,
    const double ymin, const double ymax,
    const double zmin, const double zmax,
    const ElemType elemType, std::vector<bool>& type_elem_flag) {

    SetIfHomogeneous(true);

    SetLevel(0);

    _coords.resize(3);
    

    MeshTools::Generation::BuildBox(*this, _coords, nx, ny, nz, xmin, xmax, ymin, ymax, zmin, zmax, elemType, type_elem_flag);


    AddBiquadraticNodesNotInMeshFile();

    el->ShrinkToFit();

    el->SetNodeNumber(_nnodes);  ///@todo are we sure we need it here? On the other ReadCoarse it is commented

    
    std::vector < unsigned > materialElementCounter(3, 0);
    materialElementCounter[0] = GetNumberOfElements();
    el->SetMaterialElementCounter(materialElementCounter);


    Partition();

    BuildElementAndNodeStructures();
    
  }
  

  /** This function stores the element adiacent to the element face (iel, iface)
   * and stores it in _elementNearFace[iel][iface]
   * @todo this function should go inside the Elem class instead
   **/

  void Mesh::AllocateAndMarkStructureNode() {

    _topology->ResizeSolutionVector("solidMrk");

    NumericVector& NodeMaterial =  _topology->GetSolutionName("solidMrk");

    NodeMaterial.zero();

    for(int iel = _elementOffset[_iproc]; iel < _elementOffset[_iproc + 1]; iel++) {
      int flag_mat = GetElementMaterial(iel);

      if(flag_mat == 4) { ///@todo Where on Earth do we say that 4 is a special flag for the solid
        unsigned elementType = GetElementType(iel);
        unsigned nve = el->GetNVE(elementType, 2);

        for(unsigned i = 0; i < nve; i++) {
          unsigned inode = GetSolutionDof(i, iel, 2);
          NodeMaterial.set(inode, 1);
        }
      }
    }

    NodeMaterial.close();

  }


  
  void Mesh::SetFiniteElementPtr(/*const*/ elem_type* OtherFiniteElement[N_GEOM_ELS][5]) {
      
    for(int i = 0; i < N_GEOM_ELS; i++)
      for(int j = 0; j < 5; j++)
        _finiteElement[i][j] = OtherFiniteElement[i][j];
      
  }

  
 
  

  std::vector <unsigned> Mesh::dofmap_Node_based_dof_offsets_Compute_Node_mapping_and_Node_ownSize() {
  // at this point the elements have been reordered, but not the nodes. The new node numbering starting from the med node numbering is happening here
      

     std::vector<unsigned>  partition(GetNumberOfNodes(), _nprocs);
    std::vector <unsigned>  mapping_from_mesh_file_to_femus(GetNumberOfNodes(), 0);

    for(unsigned k = 0; k < 3; k++) {
      _ownSize[k].assign(_nprocs, 0);
    }

    unsigned counter = 0;

    for(int isdom = 0; isdom < _nprocs; isdom++) {
        
      for(unsigned k = 0; k < 3; k++) {
        for(unsigned iel = _elementOffset[isdom]; iel < _elementOffset[isdom + 1]; iel++) {
          unsigned nodeStart = (k == 0) ? 0 : el->GetElementDofNumber(iel, k - 1);
          unsigned nodeEnd = el->GetElementDofNumber(iel, k);

          for(unsigned inode = nodeStart; inode < nodeEnd; inode++) {
            unsigned ii = el->GetElementDofIndex(iel, inode); //at this point the elements were reordered, but not the nodes

            if(partition[ii] > isdom) {
              partition[ii] = isdom;
              mapping_from_mesh_file_to_femus[ii] = counter;
              counter++;

              for(int j = k; j < 3; j++) {
                _ownSize[j][isdom]++;
              }
            }
          }
        }
      }
      
    }
  
    
    
    return mapping_from_mesh_file_to_femus;
  
  }
// *******************************************************


  void Mesh::initialize_elem_offsets() {
      
    _elementOffset.resize(_nprocs + 1);
    _elementOffset[0] = 0;
    
  }
  

  void Mesh::dofmap_all_fe_families_initialize() {
      
    //BEGIN Initialization for k = 0,1,2,3,4
    for(int k = 0; k < 5; k++) {
      _dofOffset[k].resize(_nprocs + 1);
      _dofOffset[k][0] = 0;
    }
    
    for(int k = 0; k < 5; k++) {
              _ownSize[k].assign(_nprocs, 0); 
            _ghostDofs[k].resize(_nprocs);
    }
    //END Initialization for k = 0,1,2,3,4

    
  }

  
   void Mesh::build_elem_offsets(const std::vector <unsigned> & partition)  {
       
          //BEGIN building the  metis2mesh_file element list 
    std::vector <unsigned>  mapping(GetNumberOfElements());

    unsigned counter = 0;

    for(int isdom = 0; isdom < _nprocs; isdom++) {  // isdom = iprocess
      for(unsigned iel = 0; iel < GetNumberOfElements(); iel++) {
        if(partition[iel] == isdom) {
          //filling the Metis to Mesh element mapping
          mapping[ iel ] = counter;
          counter++;
          _elementOffset[isdom + 1] = counter;
        }
      }
    }

    el->ReorderMeshElements(mapping);  ///this is needed because later there will be another reordering based on Group and Material
  
   }
   
   
   
  
   void Mesh::mesh_reorder_elem_quantities()  {
 


    std::vector < unsigned > imapping(GetNumberOfElements());

    for(unsigned iel = 0; iel < GetNumberOfElements(); iel++) {
      imapping[iel] = iel;
    }

    for(int isdom = 0; isdom < _nprocs; isdom++) {

// Old, much slower (while below is better) **********
//       for (unsigned i = _elementOffset[isdom]; i < _elementOffset[isdom + 1] - 1; i++) {
//         unsigned iel = imapping[i];
//         unsigned ielMat = el->GetElementMaterial (iel);
//         unsigned ielGroup = el->GetElementGroup (iel);
//         for (unsigned j = i + 1; j < _elementOffset[isdom + 1]; j++) {
//           unsigned jel = imapping[j];
//           unsigned jelMat = el->GetElementMaterial (jel);
//           unsigned jelGroup = el->GetElementGroup (jel);
//           if (jelMat < ielMat || (jelMat == ielMat && jelGroup < ielGroup || (jelGroup == ielGroup && iel > jel))) {
//             imapping[i] = jel;
//             imapping[j] = iel;
//             iel = jel;
//             ielMat = jelMat;
//             ielGroup = jelGroup;
//           }
//         }
//       }

      unsigned jel, iel;
      short unsigned jelMat, jelGroup, ielMat, ielGroup;

      unsigned n = _elementOffset[isdom + 1u] - _elementOffset[isdom];
      
      while(n > 1) {
        unsigned newN = 0u;
        for(unsigned j = _elementOffset[isdom] + 1u; j < _elementOffset[isdom] + n ; j++) {
          jel = imapping[j];
          jelMat = el->GetElementMaterial(jel);
          jelGroup = el->GetElementGroup(jel);

          iel = imapping[j - 1];
          ielMat = el->GetElementMaterial(iel);
          ielGroup = el->GetElementGroup(iel);

          if( jelMat < ielMat || (jelMat == ielMat && (jelGroup < ielGroup || (jelGroup == ielGroup && jel < iel) ) ) ) {
            imapping[j - 1] = jel;
            imapping[j] = iel;
            newN = j - _elementOffset[isdom];
          }
        }
        n = newN;
      }

    }
    

    std::vector <unsigned>  mapping(GetNumberOfElements());
        
    for(unsigned i = 0; i < GetNumberOfElements(); i++) {
      mapping[imapping[i]] = i;
    }


    el->ReorderMeshElements(mapping);
    
    //END building the  metis2mesh_file element list 

    
}



   void Mesh::dofmap_Element_based_dof_offsets_build()  {
       
    //BEGIN building element based dofs -  k = 3,4 

    // ghost vs owned nodes: 3 and 4 have no ghost nodes
    for(unsigned k = 3; k < 5; k++) {
      _ownSize[k].assign(_nprocs, 0);
    }

    for(int isdom = 0; isdom < _nprocs; isdom++) {
      _ownSize[3][isdom] = _elementOffset[isdom + 1] - _elementOffset[isdom];
      _ownSize[4][isdom] = (_elementOffset[isdom + 1] - _elementOffset[isdom]) * (_dimension + 1);
    }

    for(int k = 3; k < 5; k++) {
      _ghostDofs[k].resize(_nprocs);

      for(int isdom = 0; isdom < _nprocs; isdom++) {
        _dofOffset[k][isdom + 1] = _dofOffset[k][isdom] + _ownSize[k][isdom];
        _ghostDofs[k][isdom].resize(0);
      }
    }

    //END building element based dofs -  k = 3,4

   }
   
   

   void Mesh::dofmap_Node_based_dof_offsets_Continue_biquadratic()  {

    for(int i = 1 ; i <= _nprocs; i++) {
      _dofOffset[2][i] = _dofOffset[2][i - 1] + _ownSize[2][i - 1];
    }

   }


   void Mesh::mesh_reorder_node_quantities(const std::vector <unsigned> & mapping)  {
       
     
    el->ReorderMeshNodes(mapping);

    //reorder coordinate vector at coarse level ----
    if(GetLevel() == 0) {
      vector <double> coords_temp;

      for(int i = 0; i < 3; i++) {
        coords_temp = _coords[i];

        for(unsigned j = 0; j < GetNumberOfNodes(); j++) {
          _coords[i][mapping[j]] = coords_temp[j];
        }
      }
    }
    //reorder coordinate vector at coarse level ----


   }
   

    void Mesh::dofmap_Node_based_dof_offsets_Ghost_nodes_search_Complete_biquadratic() {
 
    for(int k = 0; k < 3; k++) {
      _ghostDofs[k].resize(_nprocs);

      for(int isdom = 0; isdom < _nprocs; isdom++) {
        std::map < unsigned, bool > ghostMap;

        for(unsigned iel = _elementOffset[isdom]; iel < _elementOffset[isdom + 1]; iel++) {
          for(unsigned inode = 0; inode < el->GetElementDofNumber(iel, k); inode++) {
            unsigned ii = el->GetElementDofIndex(iel, inode);

            if(ii < _dofOffset[2][isdom]) {
              ghostMap[ii] = true;
            }
          }
        }

        _ghostDofs[k][isdom].resize(ghostMap.size());
        unsigned counter = 0;

        for(std::map < unsigned, bool >::iterator it = ghostMap.begin(); it != ghostMap.end(); it++) {
          _ghostDofs[k][isdom][counter] = it->first;
          counter++;
        }
      }
    }

  }
  

  
    void Mesh::dofmap_Node_based_dof_offsets_Complete_linear_quadratic() {

    //BEGIN completing k = 0, 1

    for(unsigned k = 0; k < 2; k++) {

      std::vector < unsigned > ownedGhostCounter(_nprocs , 0);
      unsigned counter = 0;

      _originalOwnSize[k].resize(_nprocs);

      for(int isdom = 0; isdom < _nprocs; isdom++) {

        //owned nodes
        for(unsigned inode = _dofOffset[2][isdom]; inode < _ownSize[k][isdom] + _dofOffset[2][isdom]; inode++) {
          counter++;
        }

        for(unsigned inode = 0; inode < _ghostDofs[k][isdom].size(); inode++) {
          unsigned ghostNode = _ghostDofs[k][isdom][inode];

          unsigned ksdom = IsdomBisectionSearch(ghostNode, 2);

          int upperBound = _dofOffset[2][ksdom] + _ownSize[k][ksdom];

          if(ghostNode < upperBound) {
            _ghostDofs[k][isdom][inode] =  ghostNode  - _dofOffset[2][ksdom] + _dofOffset[k][ksdom];
          }
          else if(_ownedGhostMap[k].find(ghostNode) != _ownedGhostMap[k].end()) {
            _ghostDofs[k][isdom][inode] =  _ownedGhostMap[k][ghostNode];
          }
          else { // owned ghost nodes
            _ownedGhostMap[k][ ghostNode ] = counter;
            counter++;
            ownedGhostCounter[isdom]++;

            for(unsigned jnode = inode; jnode < _ghostDofs[k][isdom].size() - 1; jnode++) {
              _ghostDofs[k][isdom][jnode] = _ghostDofs[k][isdom][jnode + 1];
            }

            _ghostDofs[k][isdom].resize(_ghostDofs[k][isdom].size() - 1);
            inode--;
          }
        }

        _originalOwnSize[k][isdom] = _ownSize[k][isdom];
        _ownSize[k][isdom] += ownedGhostCounter[isdom];
        _dofOffset[k][isdom + 1] = _dofOffset[k][isdom] + _ownSize[k][isdom];
      }
    }

    //END completing for k = 0, 1
    

  }
  
  
  void Mesh::dofmap_all_fe_families_clear_ghost_dof_list_for_other_procs() {
      
    //delete ghost dof list all but _iproc
    for(int isdom = 0; isdom < _nprocs; isdom++) {
      if(isdom != _iproc)
        for(int k = 0; k < 5; k++) {
          _ghostDofs[k][isdom].resize(0);
        }
    }
      

  }
  
 
  void Mesh::build_dofmap_all_fe_families_and_elem_and_node_structures() {
      
            dofmap_all_fe_families_initialize();

           std::vector < unsigned > elem_partition_from_mesh_file_to_new = elem_offsets();   ///@todo these are actually unused returns
  
           std::vector < unsigned > node_mapping_from_mesh_file_to_new = node_offsets();     ///@todo these are actually unused returns
           
           dofmap_all_fe_families_clear_ghost_dof_list_for_other_procs();
       
           BuildElementAndNodeStructures();

  }

  
  
  void Mesh::set_node_counts() {
      
    SetNumberOfNodes(_dofOffset[2][_nprocs]);
    el->SetNodeNumber(_dofOffset[2][_nprocs]);

  }
  

  void Mesh::set_elem_counts() {
      
    el->SetElementOffsets(_elementOffset, _iproc, _nprocs);

  }
  
  
  void Mesh::deallocate_node_mapping(std::vector < unsigned > & node_mapping) const {
      
    std::vector<unsigned> ().swap(node_mapping);    //     node_mapping.resize(0);  //resize DOES NOT FREE memory!!!
      
  }

  
  
  // // // ======== ELEM OFFSETS =========================================================  
  void Mesh::FillISvectorElemOffsets(std::vector < unsigned >& partition) {
      
   
   initialize_elem_offsets();
   build_elem_offsets(partition);
   mesh_reorder_elem_quantities();
   set_elem_counts();
   
      
  }

  std::vector < unsigned >  Mesh::elem_offsets() {

      std::vector < unsigned > elem_partition_from_mesh_file_to_new = PartitionForElements(); 
           
      FillISvectorElemOffsets(elem_partition_from_mesh_file_to_new);
           
      dofmap_Element_based_dof_offsets_build();
      
      return elem_partition_from_mesh_file_to_new;
      
}
  
  
  // // // ======== NODE OFFSETS =========================================================  
  void Mesh::FillISvectorNodeOffsets() {
      
    std::vector < unsigned > node_mapping =  dofmap_Node_based_dof_offsets_Compute_Node_mapping_and_Node_ownSize();
    mesh_reorder_node_quantities(node_mapping);
    
    deallocate_node_mapping(node_mapping);
      
  }
  
  std::vector < unsigned >  Mesh::node_offsets() {
      
           std::vector < unsigned > node_mapping_from_mesh_file_to_new = dofmap_Node_based_dof_offsets_Compute_Node_mapping_and_Node_ownSize();
           
           mesh_reorder_node_quantities(node_mapping_from_mesh_file_to_new);
           
           dofmap_Node_based_dof_offsets_Continue_biquadratic();
           dofmap_Node_based_dof_offsets_Ghost_nodes_search_Complete_biquadratic();
           dofmap_Node_based_dof_offsets_Complete_linear_quadratic();
           
           set_node_counts();
  
      return node_mapping_from_mesh_file_to_new;
}  
 /**
  * dof map: piecewise liner 0, quadratic 1, bi-quadratic 2, piecewise constant 3, piecewise linear discontinuous 4
  */
  void Mesh::FillISvector(std::vector < unsigned >& partition) {

        dofmap_all_fe_families_initialize();
     
     FillISvectorElemOffsets(partition);
     
        dofmap_Element_based_dof_offsets_build();  

      //BEGIN building for k = 0,1,2
     FillISvectorNodeOffsets();
    
      dofmap_Node_based_dof_offsets_Continue_biquadratic();
        //BEGIN ghost nodes search k = 0, 1, 2
      dofmap_Node_based_dof_offsets_Ghost_nodes_search_Complete_biquadratic();
        //END ghost nodes search k = 0, 1, 2
      //END building for k = 2, but incomplete for k = 0, 1
      //BEGIN completing for k = 0,1
      dofmap_Node_based_dof_offsets_Complete_linear_quadratic();
      //END completing for k = 0,1
    
    set_node_counts(); //also, it shouldn't use dofOffset

     
     
        dofmap_all_fe_families_clear_ghost_dof_list_for_other_procs();
    
  }


// *******************************************************
  unsigned Mesh::IsdomBisectionSearch(const unsigned& dof, const short unsigned& solType) const {

    unsigned isdom0 = 0;
    unsigned isdom1 = _nprocs ;
    unsigned isdom = _iproc;

    while(dof < _dofOffset[solType][isdom] || dof >= _dofOffset[solType][isdom + 1]) {
      if(dof < _dofOffset[solType][isdom]) isdom1 = isdom;
      else isdom0 = isdom + 1;

      isdom = (isdom0 + isdom1) / 2;
    }

    return isdom;
  }
// *******************************************************

  unsigned Mesh::GetSolutionDof(const unsigned& i, const unsigned& iel, const short unsigned& solType) const {

    unsigned dof;

    switch(solType) {
        
      case 0: { // linear Lagrange
        unsigned iNode = el->GetElementDofIndex(iel, i);  //GetMeshDof(iel, i, solType);
        unsigned isdom = IsdomBisectionSearch(iNode, 2);

        if(iNode < _dofOffset[2][isdom] + _originalOwnSize[0][isdom]) {
          dof = (iNode - _dofOffset[2][isdom]) + _dofOffset[0][isdom];
        }
        else {
          dof = _ownedGhostMap[0].find(iNode)->second;
        }
      }
      break;

      case 1: { // quadratic Lagrange
        unsigned iNode = el->GetElementDofIndex(iel, i);  //GetMeshDof(iel, i, solType);
        unsigned isdom = IsdomBisectionSearch(iNode, 2);

        if(iNode < _dofOffset[2][isdom] + _originalOwnSize[1][isdom]) {
          dof = (iNode - _dofOffset[2][isdom]) + _dofOffset[1][isdom];
        }
        else {
          dof = _ownedGhostMap[1].find(iNode)->second;
        }
      }
      break;

      case 2: // bi-quadratic Lagrange
        dof = el->GetElementDofIndex(iel, i);  //GetMeshDof(iel, i, solType);
        break;

      case 3: // piecewise constant
        // in this case use i=0
        dof = iel;
        break;

      case 4: // piecewise linear discontinuous
        unsigned isdom = IsdomBisectionSearch(iel, 3);
        unsigned offset = _elementOffset[isdom];
        unsigned offsetp1 = _elementOffset[isdom + 1];
        unsigned ownSize = offsetp1 - offset;
        unsigned offsetPWLD = offset * (_dimension + 1);
        unsigned locIel = iel - offset;
        dof = offsetPWLD + (i * ownSize) + locIel;
        break;
    }

    return dof;
  }

// *******************************************************

  unsigned Mesh::GetSolutionDof(const unsigned& ielc, const unsigned& i0, const unsigned& i1, const short unsigned& solType, const Mesh* mshc) const {

    unsigned dof;

    switch(solType) {
        
      case 0: { // linear Lagrange
        unsigned iNode = mshc->el->GetChildElementDof(ielc, i0, i1);
        unsigned isdom = IsdomBisectionSearch(iNode, 2);

        if(iNode < _dofOffset[2][isdom] + _originalOwnSize[0][isdom]) {
          dof = (iNode - _dofOffset[2][isdom]) + _dofOffset[0][isdom];
        }
        else {
          dof = _ownedGhostMap[0].find(iNode)->second;
        }
      }
      break;

      case 1: { // quadratic Lagrange
        unsigned iNode = mshc->el->GetChildElementDof(ielc, i0, i1);
        unsigned isdom = IsdomBisectionSearch(iNode, 2);

        if(iNode < _dofOffset[2][isdom] + _originalOwnSize[1][isdom]) {
          dof = (iNode - _dofOffset[2][isdom]) + _dofOffset[1][isdom];
        }
        else {
          dof = _ownedGhostMap[1].find(iNode)->second;
        }
      }
      break;

      case 2: // bi-quadratic Lagrange
        dof = mshc->el->GetChildElementDof(ielc, i0, i1);
        break;

      case 3: // piecewise constant
        // in this case use i=0
        dof = mshc->el->GetChildElement(ielc, i0);
        break;

      case 4: // piecewise linear discontinuous
        unsigned iel = mshc->el->GetChildElement(ielc, i0);
        unsigned isdom = IsdomBisectionSearch(iel, 3);
        unsigned offset = _elementOffset[isdom];
        unsigned offsetp1 = _elementOffset[isdom + 1];
        unsigned ownSize = offsetp1 - offset;
        unsigned offsetPWLD = offset * (_dimension + 1);
        unsigned locIel = iel - offset;
        dof = offsetPWLD + (i1 * ownSize) + locIel;
        break;
    }

    return dof;
  }

// *******************************************************


  SparseMatrix* Mesh::GetQitoQjProjection(const unsigned& itype, const unsigned& jtype) {
      
    if(itype < 3 && jtype < 3) {
      if(!_ProjQitoQj[itype][jtype]) {
        BuildQitoQjProjection(itype, jtype);
      }
    }
    else {
      std::cout << "Wrong argument range in function"
                << "Mesh::GetLagrangeProjectionMatrix(const unsigned& itype, const unsigned& jtype)" << std::endl;
      abort();
    }

    return _ProjQitoQj[itype][jtype];
  }

  void Mesh::BuildQitoQjProjection(const unsigned& itype, const unsigned& jtype) {

    unsigned ni = _dofOffset[itype][_nprocs];
    unsigned ni_loc = _ownSize[itype][_iproc];

    unsigned nj = _dofOffset[jtype][_nprocs];
    unsigned nj_loc = _ownSize[jtype][_iproc];

    NumericVector* NNZ_d = NumericVector::build().release();

    if(1 == _nprocs) {  // IF SERIAL
      NNZ_d->init(ni, ni_loc, false, SERIAL);
    }
    else {
      NNZ_d->init(ni, ni_loc, _ghostDofs[itype][processor_id()], false, GHOSTED);
    }

    NNZ_d->zero();

    NumericVector* NNZ_o = NumericVector::build().release();
    NNZ_o->init(*NNZ_d);
    NNZ_o->zero();

    for(unsigned isdom = _iproc; isdom < _iproc + 1; isdom++) {
      for(unsigned iel = _elementOffset[isdom]; iel < _elementOffset[isdom + 1]; iel++) {
        short unsigned ielt = GetElementType(iel);
        _finiteElement[ielt][jtype]->GetSparsityPatternSize(*this, iel, NNZ_d, NNZ_o, itype);
      }
    }

    NNZ_d->close();
    NNZ_o->close();

    unsigned offset = _dofOffset[itype][_iproc];

    vector < int > nnz_d(ni_loc);
    vector < int > nnz_o(ni_loc);

    for(unsigned i = 0; i < ni_loc; i++) {
      nnz_d[i] = static_cast < int >((*NNZ_d)(offset + i));
      nnz_o[i] = static_cast < int >((*NNZ_o)(offset + i));
    }

    _ProjQitoQj[itype][jtype] = SparseMatrix::build().release();
    _ProjQitoQj[itype][jtype]->init(ni, nj, ni_loc, nj_loc, nnz_d, nnz_o);

    for(unsigned isdom = _iproc; isdom < _iproc + 1; isdom++) {
      for(unsigned iel = _elementOffset[isdom]; iel < _elementOffset[isdom + 1]; iel++) {
        short unsigned ielt = GetElementType(iel);
        _finiteElement[ielt][jtype]->BuildProlongation(*this, iel, _ProjQitoQj[itype][jtype], NNZ_d, NNZ_o, itype);
      }
    }

    _ProjQitoQj[itype][jtype]->close();

    delete NNZ_d;
    delete NNZ_o;
  }


  SparseMatrix* Mesh::GetCoarseToFineProjectionRestrictionOnCoarse(const unsigned& solType) {

    if(solType >= 5) {
      std::cout << "Wrong argument range in function \"GetCoarseToFineProjection\": "
                << "solType is greater then SolTypeMax" << std::endl;
      abort();
    }

    if(_ProjCoarseToFine[solType])
      BuildCoarseToFineProjection(solType, "coarse");

    return _ProjCoarseToFine[solType];
  }


  SparseMatrix* Mesh::GetCoarseToFineProjection(const unsigned& solType) {

    if(solType >= 5) {
      std::cout << "Wrong argument range in function \"GetCoarseToFineProjection\": "
                << "solType is greater then SolTypeMax" << std::endl;
      abort();
    }

    if(!_ProjCoarseToFine[solType])
      BuildCoarseToFineProjection(solType, "fine");

    return _ProjCoarseToFine[solType];
  }



  void Mesh::BuildCoarseToFineProjection(const unsigned& solType, const char el_dofs[]) {

    if(!_coarseMsh) {
      std::cout << "Error! In function \"BuildCoarseToFineProjection\": the coarse mesh has not been set" << std::endl;
      abort();
    }

    if(!_ProjCoarseToFine[solType]) {

      int nf     = _dofOffset[solType][_nprocs];
      int nc     = _coarseMsh->_dofOffset[solType][_nprocs];
      int nf_loc = _ownSize[solType][_iproc];
      int nc_loc = _coarseMsh->_ownSize[solType][_iproc];

      //build matrix sparsity pattern size
      NumericVector* NNZ_d = NumericVector::build().release();

      if(n_processors() == 1) {  // IF SERIAL
        NNZ_d->init(nf, nf_loc, false, SERIAL);
      }
      else { // IF PARALLEL
        if(solType < 3) {  // GHOST nodes only for Lagrange FE families
          NNZ_d->init(nf, nf_loc, _ghostDofs[solType][processor_id()], false, GHOSTED);
        }
        else { //piecewise discontinuous variables have no ghost nodes
          NNZ_d->init(nf, nf_loc, false, PARALLEL);
        }
      }

      NNZ_d->zero();

      NumericVector* NNZ_o = NumericVector::build().release();
      NNZ_o->init(*NNZ_d);
      NNZ_o->zero();

      for(int isdom = _iproc; isdom < _iproc + 1; isdom++) {
        for(int iel = _coarseMsh->_elementOffset[isdom]; iel < _coarseMsh->_elementOffset[isdom + 1]; iel++) {
          short unsigned ielt = _coarseMsh->GetElementType(iel);
          _finiteElement[ielt][solType]->GetSparsityPatternSize(*this, *_coarseMsh, iel, NNZ_d, NNZ_o, el_dofs);
        }
      }

      NNZ_d->close();
      NNZ_o->close();

      unsigned offset = _dofOffset[solType][_iproc];
      vector <int> nnz_d(nf_loc);
      vector <int> nnz_o(nf_loc);

      for(int i = 0; i < nf_loc; i++) {
        nnz_d[i] = static_cast <int>(floor((*NNZ_d)(offset + i) + 0.5));
        nnz_o[i] = static_cast <int>(floor((*NNZ_o)(offset + i) + 0.5));
      }

      delete NNZ_d;
      delete NNZ_o;

      //build matrix
      _ProjCoarseToFine[solType] = SparseMatrix::build().release();
      _ProjCoarseToFine[solType]->init(nf, nc, nf_loc, nc_loc, nnz_d, nnz_o);

      // loop on the coarse grid
      for(int isdom = _iproc; isdom < _iproc + 1; isdom++) {
        for(int iel = _coarseMsh->_elementOffset[isdom]; iel < _coarseMsh->_elementOffset[isdom + 1]; iel++) {
          short unsigned ielt = _coarseMsh->GetElementType(iel);
          _finiteElement[ielt][solType]->BuildProlongation(*this, *_coarseMsh, iel, _ProjCoarseToFine[solType], el_dofs);
        }
      }

      _ProjCoarseToFine[solType]->close();
    }

  }


  short unsigned Mesh::GetRefinedElementIndex(const unsigned& iel) const {
    return static_cast <short unsigned>((*_topology->_Sol[_amrIndex])(iel) + 0.25);
  }

  short unsigned Mesh::GetElementGroup(const unsigned int& iel) const {
    return el->GetElementGroup(iel);
  }

  short unsigned Mesh::GetElementMaterial(const unsigned int& iel) const {
    return el->GetElementMaterial(iel);
  }

  short unsigned Mesh::GetElementType(const unsigned int& iel) const {
    return el->GetElementType(iel);
  }

  bool Mesh::GetSolidMark(const unsigned int& inode) const {
    return static_cast <short unsigned>((*_topology->_Sol[_solidMarkIndex])(inode) + 0.25);
  }


  /** Only for parallel */
  unsigned Mesh::GetElementDofNumber(const unsigned& iel, const unsigned& type) const {
    return el->GetNVE(GetElementType(iel), type);
  }

  /** Only for parallel */
  const unsigned Mesh::GetElementFaceType(const unsigned& kel, const unsigned& jface) const {
    unsigned kelt = GetElementType(kel);
    const unsigned FELT[6][2] = {{3, 3}, {4, 4}, {3, 4}, {5, 5}, {5, 5}, {6, 6}};
    const unsigned felt = FELT[kelt][jface >= GetElementFaceNumber(kel, 0)];
    return felt;
  }

  /** Only for parallel */
  unsigned Mesh::GetLocalFaceVertexIndex(const unsigned& iel, const unsigned& iface, const unsigned& jnode) const {
    return el->GetIG(GetElementType(iel), iface, jnode);
  }

  /** this needs the abstract elem type */
  unsigned Mesh::GetLocalFaceVertexIndex_PassElemType(const short unsigned & el_type, const unsigned& iface, const unsigned& jnode) const {
    return el->GetIG(el_type, iface, jnode);
  }


  /** Only for parallel */
  unsigned Mesh::GetElementFaceDofNumber(const unsigned& iel, const unsigned jface, const unsigned& type) const {
    assert(type < 3);   ///@todo relax this
    return el->GetNFACENODES(GetElementType(iel), jface, type);
  }

  /** Only for parallel */
  unsigned Mesh::GetElementFaceNumber(const unsigned& iel, const unsigned& type) const {
    return el->GetNFC(GetElementType(iel), type);
  }
  
  /** this needs the abstract elem type */
  unsigned Mesh::GetElementFaceNumber_PassElemType(const short unsigned & el_type, const unsigned& type) const {
    return el->GetNFC(el_type, type);
  }

// *******************************************************

  void Mesh::AddBiquadraticNodesNotInMeshFile() {

    unsigned int nnodes = GetNumberOfNodes();

    //intialize to UINT_MAX
    for(unsigned iel = 0; iel < el->GetElementNumber(); iel++) {
      unsigned elementType = el->GetElementType(iel);

      if(elementType == 1 || elementType == 2) {
        for(unsigned inode = el->GetElementDofNumber(iel, 2) - _numberOfMissedBiquadraticNodes[elementType];
            inode < el->GetElementDofNumber(iel, 2); inode++) {
          el->SetElementDofIndex(iel, inode, UINT_MAX);
        }
      }
    }

    // generate face dofs for tet and wedge elements
    for(unsigned iel = 0; iel < el->GetElementNumber(); iel++) {
      unsigned elementType = el->GetElementType(iel);

      if(elementType == 1 || elementType == 2) {
        for(unsigned iface = el->GetElementFaceNumber(iel, 0); iface < el->GetElementFaceNumber(iel, 1); iface++) {       //on all the faces that are triangles
          unsigned inode = el->GetElementDofNumber(iel, 1) + iface;

          if(UINT_MAX == el->GetElementDofIndex(iel, inode)) {
            el->SetElementDofIndex(iel, inode, nnodes);
            unsigned i1 = el->GetFaceVertexIndex(iel, iface, 0);
            unsigned i2 = el->GetFaceVertexIndex(iel, iface, 1);
            unsigned i3 = el->GetFaceVertexIndex(iel, iface, 2);
            bool faceHasBeenFound = false;

            for(unsigned jel = iel + 1; jel < el->GetElementNumber(); jel++) {
              for(unsigned jface = el->GetElementFaceNumber(jel, 0); jface < el->GetElementFaceNumber(jel, 1); jface++) {
                unsigned jnode = el->GetElementDofNumber(jel, 1) + jface;

                if(UINT_MAX == el->GetElementDofIndex(jel, jnode)) {
                  unsigned j1 = el->GetFaceVertexIndex(jel, jface, 0);
                  unsigned j2 = el->GetFaceVertexIndex(jel, jface, 1);
                  unsigned j3 = el->GetFaceVertexIndex(jel, jface, 2);

                  if((i1 == j1 || i1 == j2 || i1 == j3) &&
                      (i2 == j1 || i2 == j2 || i2 == j3) &&
                      (i3 == j1 || i3 == j2 || i3 == j3)) {
                    el->SetElementDofIndex(jel, jnode, nnodes);
                    faceHasBeenFound = true;
                    break;
                  }
                }
              }

              if(faceHasBeenFound) {
                break;
              }
            }

            ++nnodes;
          }
        }
      }
    }

    // generates element dofs for tet, wedge and triangle elements
    for(unsigned iel = 0; iel < el->GetElementNumber(); iel++) {
      if(1 == el->GetElementType(iel)) {     //tet
        el->SetElementDofIndex(iel, 14, nnodes);
        ++nnodes;
      }
      else if(2 == el->GetElementType(iel)) {     //wedge
        el->SetElementDofIndex(iel, 20, nnodes);
        ++nnodes;
      }
      else if(4 == el->GetElementType(iel)) {     //triangle
        el->SetElementDofIndex(iel, 6, nnodes);
        ++nnodes;
      }
    }

    el->SetNodeNumber(nnodes);
    SetNumberOfNodes(nnodes);
//     std::cout <<"nnodes after="<< nnodes << std::endl;
    
    

    // add the coordinates of the biquadratic nodes not included in gambit
    _coords[0].resize(nnodes);
    _coords[1].resize(nnodes);
    _coords[2].resize(nnodes);

    for(int iel = 0; iel < el->GetElementNumber(); iel++) {
      unsigned elementType = el->GetElementType(iel);

      unsigned ndof = el->GetElementDofNumber(iel, 2);
      unsigned jstart = ndof - _numberOfMissedBiquadraticNodes[elementType];

      for(unsigned j = jstart; j < ndof; j++) {

        unsigned jnode = el->GetElementDofIndex(iel, j);

        _coords[0][jnode] = 0.;
        _coords[1][jnode] = 0.;
        _coords[2][jnode] = 0.;

        for(int i = 0; i < jstart; i++) {
          unsigned inode = el->GetElementDofIndex(iel, i);

          for(int k = 0; k < GetDimension(); k++) {
            _coords[k][jnode] += _coords[k][inode] * _baricentricWeight[ elementType ][j - jstart][i];
          }
        }
      }
    }
    
    
  }
  
  

  basis* Mesh::GetBasis(const short unsigned& ielType, const short unsigned& solType) {
    return _finiteElement[ielType][solType]->GetBasis();
  }

  
  void Mesh::GetElementNodeCoordinates(std::vector < std::vector <double > > &xv, const unsigned &iel, const unsigned &solType) {
      
    xv.resize(_dimension);
    unsigned ndofs = el->GetElementDofNumber(iel, solType);
    for(int d = 0; d < _dimension; d++) {
      xv[d].resize(ndofs);
    }
    for(unsigned j = 0; j < ndofs; j++) {
      unsigned xdof  = GetSolutionDof(j, iel, solType);
      for(int d = 0; d < _dimension; d++) {
        xv[d][j] = (*_topology->_Sol[d])(xdof);
      }
    }
    
  }

  
  
  
} //end namespace femus
