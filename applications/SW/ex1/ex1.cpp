/** tutorial/Ex1
 * This example shows how to:
 * initialize a femus application;
 * define the multilevel-mesh object mlMsh;
 * read from the file ./input/square.neu the coarse-level mesh and associate it to mlMsh;
 * add in mlMsh uniform refined level-meshes;
 * define the multilevel-solution object mlSol associated to mlMsh;
 * add in mlSol different types of finite element solution variables;
 * initialize the solution varables;
 * define vtk and gmv writer objects associated to mlSol;
 * print vtk and gmv binary-format files in ./output directory.
 **/

#include "FemusInit.hpp"
#include "MultiLevelSolution.hpp"
#include "MultiLevelProblem.hpp"
#include "VTKWriter.hpp"
#include "GMVWriter.hpp"
#include "PetscMatrix.hpp"

#include "LinearImplicitSystem.hpp"

#include "slepceps.h"
#include <slepcmfn.h>

using namespace femus;

double rho[10]={1025,1027,1028};

double InitalValueV(const std::vector < double >& x)
{
  return 0;
}


double InitalValueH(const std::vector < double >& x)
{
  return 2.* exp(-(x[0] / 100000.) * (x[0] / 100000.));
}


bool SetBoundaryCondition(const std::vector < double >& x, const char SolName[], double& value, const int facename, const double time)
{
  bool dirichlet = false; //dirichlet
  if( facename == 1 || facename == 2) dirichlet = true;
  value = 0.;
  return dirichlet;
}


void ETD(MultiLevelProblem& ml_prob, const unsigned& numberOfLayers);


int main(int argc, char** args)
{

  SlepcInitialize(&argc, &args, PETSC_NULL, PETSC_NULL);

  // init Petsc-MPI communicator
  FemusInit mpinit(argc, args, MPI_COMM_WORLD);

  // define multilevel mesh
  MultiLevelMesh mlMsh;
  double scalingFactor = 1.;



  unsigned numberOfUniformLevels = 1;
  unsigned numberOfSelectiveLevels = 0;

  unsigned nx = static_cast<unsigned>(floor(pow(2.,9) + 0.5));
  
  double length = 2. * 1465700.;

  mlMsh.GenerateCoarseBoxMesh(nx, 0, 0, -length / 2, length / 2, 0., 0., 0., 0., EDGE3, "seventh");

  //mlMsh.RefineMesh(numberOfUniformLevels + numberOfSelectiveLevels, numberOfUniformLevels , NULL);
  mlMsh.PrintInfo();

  // define the multilevel solution and attach the mlMsh object to it
  MultiLevelSolution mlSol(&mlMsh);

  unsigned NumberOfLayers = 1;

  for(unsigned i = 0; i < NumberOfLayers; i++) {
    char name[10];
    sprintf(name, "h%d", i);
    mlSol.AddSolution(name, DISCONTINUOUS_POLYNOMIAL, ZERO);
    sprintf(name, "v%d", i);
    mlSol.AddSolution(name, LAGRANGE, FIRST);
    //sprintf(name, "he%d", i);
    //mlSol.AddSolution(name, LAGRANGE, FIRST);
  }

  for(unsigned i = 0; i < NumberOfLayers; i++) {
    char name[10];
    sprintf(name, "h%d", i);
    mlSol.Initialize(name, InitalValueH);
    sprintf(name, "v%d", i);
    mlSol.Initialize(name, InitalValueV);
    //sprintf(name, "he%d", i);
    //mlSol.Initialize(name, InitalValueH);
  }

  //mlSol.Initialize("All");    // initialize all varaibles to zero

  mlSol.AttachSetBoundaryConditionFunction(SetBoundaryCondition);
  mlSol.GenerateBdc("All");

  MultiLevelProblem ml_prob(&mlSol);

  // ******* Add FEM system to the MultiLevel problem *******
  LinearImplicitSystem& system = ml_prob.add_system < LinearImplicitSystem > ("SW");
  for(unsigned i = 0; i < NumberOfLayers; i++) {
    char name[10];
    sprintf(name, "h%d", i);
    system.AddSolutionToSystemPDE(name);
    sprintf(name, "v%d", i);
    system.AddSolutionToSystemPDE(name);
    //sprintf(name, "he%d", i);
    //system.AddSolutionToSystemPDE(name);
  }
  system.init();

  mlSol.SetWriter(VTK);
  std::vector<std::string> print_vars;
  print_vars.push_back("All");
  //mlSol.GetWriter()->SetDebugOutput(true);
  mlSol.GetWriter()->Write(DEFAULT_OUTPUTDIR, "linear", print_vars, 0);

  unsigned numberOfTimeSteps = 2000;
  for(unsigned i = 0; i < numberOfTimeSteps; i++) {
    ETD(ml_prob, NumberOfLayers);
    mlSol.GetWriter()->Write(DEFAULT_OUTPUTDIR, "linear", print_vars, i + 1);
  }



  return 0;
}


void ETD(MultiLevelProblem& ml_prob, const unsigned& NLayers)
{

  double aa = 6371000;  //radius of earth [m]
  double H_0 = 2400;    // max depth of ocean [m]
  double H_shelf = 100; //depth of continental shelf [m]

  double z_c = aa;      //z coordinate of center point
  double bb = 1250000;

  double phi = 0.1;     //relative width of continental shelf: 10%

  adept::Stack& s = FemusInit::_adeptStack;

  LinearImplicitSystem* mlPdeSys  = &ml_prob.get_system<LinearImplicitSystem> ("SW");   // pointer to the linear implicit system named "Poisson"

  unsigned level = ml_prob._ml_msh->GetNumberOfLevels() - 1u;

  Mesh* msh = ml_prob._ml_msh->GetLevel(level);    // pointer to the mesh (level) object
  elem* el = msh->el;  // pointer to the elem object in msh (level)

  MultiLevelSolution* mlSol = ml_prob._ml_sol;  // pointer to the multilevel solution object
  Solution* sol = ml_prob._ml_sol->GetSolutionLevel(level);    // pointer to the solution (level) object

  LinearEquationSolver* pdeSys = mlPdeSys->_LinSolver[level]; // pointer to the equation (level) object

  SparseMatrix* KK = pdeSys->_KK;  // pointer to the global stifness matrix object in pdeSys (level)
  NumericVector* RES = pdeSys->_RES; // pointer to the global residual vector object in pdeSys (level)
  NumericVector* EPS = pdeSys->_EPS; // pointer to the global residual vector object in pdeSys (level)

  const unsigned  dim = msh->GetDimension(); // get the domain dimension of the problem

  unsigned    iproc = msh->processor_id(); // get the process_id (for parallel computation)
  unsigned    nprocs = msh->n_processors(); // get the process_id (for parallel computation)


  //solution variable
  std::vector < unsigned > solIndexh(NLayers);
  std::vector < unsigned > solPdeIndexh(NLayers);

  std::vector < unsigned > solIndexv(NLayers);
  std::vector < unsigned > solPdeIndexv(NLayers);

  //std::vector < unsigned > solIndexhe(NLayers);
  //std::vector < unsigned > solPdeIndexhe(NLayers);

  vector< int > l2GMap; // local to global mapping

  for(unsigned i = 0; i < NLayers; i++) {
    char name[10];
    sprintf(name, "h%d", i);
    solIndexh[i] = mlSol->GetIndex(name); // get the position of "hi" in the sol object
    solPdeIndexh[i] = mlPdeSys->GetSolPdeIndex(name); // get the position of "hi" in the pdeSys object

    sprintf(name, "v%d", i);
    solIndexv[i] = mlSol->GetIndex(name); // get the position of "vi" in the sol object
    solPdeIndexv[i] = mlPdeSys->GetSolPdeIndex(name); // get the position of "vi" in the pdeSys object

//     sprintf(name, "he%d", i);
//     solIndexhe[i] = mlSol->GetIndex(name); // get the position of "vi" in the sol object
    //solPdeIndexhe[i] = mlPdeSys->GetSolPdeIndex(name); // get the position of "vi" in the pdeSys object

  }

  unsigned solTypeh = mlSol->GetSolutionType(solIndexh[0]);    // get the finite element type for "hi"
  unsigned solTypev = mlSol->GetSolutionType(solIndexv[0]);    // get the finite element type for "vi"
  //unsigned solTypehe = mlSol->GetSolutionType(solIndexhe[0]);    // get the finite element type for "hi"

  vector < double > x;    // local coordinates
  vector< vector < adept::adouble > > solh(NLayers);    // local coordinates
  vector< vector < adept::adouble > > solv(NLayers);    // local coordinates
  //vector< vector < adept::adouble > > solhe(NLayers);    // local coordinates

  vector< vector < bool > > bdch(NLayers);    // local coordinates
  vector< vector < bool > > bdcv(NLayers);    // local coordinates
  //vector< vector < bool > > bdche(NLayers);    // local coordinates

  unsigned xType = 2; // get the finite element type for "x", it is always 2 (LAGRANGE QUADRATIC)

  vector < vector< adept::adouble > > aResh(NLayers);
  vector < vector< adept::adouble > > aResv(NLayers);
  //vector < vector< adept::adouble > > aReshe(NLayers);

  KK->zero();
  RES->zero();

  double maxWaveSpeed = 0.;
  
  double dx;
    
  for(int iel = msh->_elementOffset[iproc]; iel < msh->_elementOffset[iproc + 1]; iel++) {

    short unsigned ielGeom = msh->GetElementType(iel);
    unsigned nDofh  = msh->GetElementDofNumber(iel, solTypeh);    // number of solution element dofs
    unsigned nDofv  = msh->GetElementDofNumber(iel, solTypev);    // number of solution element dofs
    //unsigned nDofhe  = msh->GetElementDofNumber(iel, solTypehe);    // number of solution element dofs
    unsigned nDofx = msh->GetElementDofNumber(iel, xType);    // number of coordinate element dofs

    unsigned nDofs = nDofh + nDofv;// + nDofhe;

    // resize local arrays
    l2GMap.resize(NLayers * (nDofs) );

    for(unsigned i = 0; i < NLayers; i++) {
      solh[i].resize(nDofh);
      solv[i].resize(nDofv);
      //solhe[i].resize(nDofhe);
      bdch[i].resize(nDofh);
      bdcv[i].resize(nDofv);
      //bdche[i].resize(nDofhe);

      aResh[i].resize(nDofh);    //resize
      std::fill(aResh[i].begin(), aResh[i].end(), 0);    //set aRes to zero
      aResv[i].resize(nDofv);    //resize
      std::fill(aResv[i].begin(), aResv[i].end(), 0);    //set aRes to zero
//       aReshe[i].resize(nDofhe);    //resize
//       std::fill(aReshe[i].begin(), aReshe[i].end(), 0);    //set aRes to zero
    }
    x.resize(nDofx);

    //local storage of global mapping and solution
    for(unsigned i = 0; i < nDofh; i++) {
      unsigned solDofh = msh->GetSolutionDof(i, iel, solTypeh);    // global to global mapping between solution node and solution dof
      for(unsigned j = 0; j < NLayers; j++) {
        solh[j][i] = (*sol->_Sol[solIndexh[j]])(solDofh);      // global extraction and local storage for the solution
        bdch[j][i] = ( (*sol->_Bdc[solIndexh[j]])(solDofh) < 1.5) ? true : false;
        l2GMap[ j * nDofs + i] = pdeSys->GetSystemDof(solIndexh[j], solPdeIndexh[j], i, iel);    // global to global mapping between solution node and pdeSys dof
      }
    }
    for(unsigned i = 0; i < nDofv; i++) {
      unsigned solDofv = msh->GetSolutionDof(i, iel, solTypev);    // global to global mapping between solution node and solution dof
      for(unsigned j = 0; j < NLayers; j++) {
        solv[j][i] = (*sol->_Sol[solIndexv[j]])(solDofv);      // global extraction and local storage for the solution
        bdcv[j][i] = ( (*sol->_Bdc[solIndexv[j]])(solDofv) < 1.5) ? true : false;
        l2GMap[ j * nDofs + nDofh + i] = pdeSys->GetSystemDof(solIndexv[j], solPdeIndexv[j], i, iel);    // global to global mapping between solution node and pdeSys dof
      }
    }
//     for(unsigned i = 0; i < nDofhe; i++) {
//       unsigned solDofhe = msh->GetSolutionDof(i, iel, solTypehe);    // global to global mapping between solution node and solution dof
//       for(unsigned j = 0; j < NLayers; j++) {
//         solhe[j][i] = (*sol->_Sol[solIndexhe[j]])(solDofhe);      // global extraction and local storage for the solution
//         bdche[j][i] = ( (*sol->_Bdc[solIndexhe[j]])(solDofhe) < 1.5) ? true : false;
//         //l2GMap[ j * nDofs + nDofh + nDofv + i] = pdeSys->GetSystemDof(solIndexhe[j], solPdeIndexhe[j], i, iel);    // global to global mapping between solution node and pdeSys dof
//       }
//     }


   

    s.new_recording();
    
    for(unsigned i = 0; i < nDofx; i++) {
      unsigned xDof  = msh->GetSolutionDof(i, iel, xType);    // global to global mapping between coordinates node and coordinate dof
      x[i] = (*msh->_topology->_Sol[0])(xDof);      // global extraction and local storage for the element coordinates
    }
    

    double xmid = 0.5 * (x[0] + x[1]);
    //std::cout << xmid << std::endl;
    double zz = sqrt(aa * aa - xmid * xmid); // z coordinate of points on sphere
    double dd = aa * acos((zz * z_c) / (aa * aa)); // distance to center point on sphere [m]
    double hh = 1 - dd * dd / (bb * bb);
    std::vector < adept::adouble > b1(NLayers);
    for(unsigned k = 0; k < NLayers; k++) {
       b1[k] = ( H_shelf + H_0 / 2 * (1 + tanh(hh / phi)) ) * rho[k]; // bottom topography
       
       for( unsigned j = 0; j < NLayers; j++){
	 double rhoj = (j <= k) ? rho[j] : rho[k];
	 b1[k] += rhoj * solh[j][0];
      }
      b1[k] /= rho[k];
    }
    //std::cout << b1[0] << std::endl;
    
    for(unsigned i = 0; i<NLayers; i++){
      //double celerity = sqrt( 9.81 * solh[i][0].value());
      double celerity = sqrt( 9.81 * b1[NLayers-1].value());
      double vmid = 0.5 * ( solv[i][0].value() + solv[i][1].value() );
      maxWaveSpeed = ( maxWaveSpeed > fabs(vmid) + fabs(celerity) )?  maxWaveSpeed : fabs(vmid) + fabs(celerity);
    }
    std::cout<<maxWaveSpeed<<std::endl;
    
//     b1[NLayers - 1] = H_shelf + H_0 / 2 * (1 + tanh(hh / phi))*0; // bottom topography
//     for(unsigned i = NLayers - 1; i > 0; i--) {
//       b1[i - 1] = b1[i] + solh[i][0]; // bottom topography
//     }


    dx = x[1] - x[0];

    for(unsigned k = 0; k < NLayers; k++) {
      for (unsigned i = 0; i < nDofh; i++) {
        if(!bdch[k][i]) {
          for (unsigned j = 0; j < nDofv; j++) {
            double sign = ( j == 0) ? 1. : -1;
            //aResh[k][i] += sign * solh[k][i] * solv[k][j] / dx;
	    aResh[k][i] += sign * b1[k] * solv[k][j] / dx;
          }
        }
      }
      adept::adouble vMid = 0.5 * (solv[k][0] + solv[k][1]);
      //adept::adouble fv = 0.5 * vMid * vMid + 9.81 * b1[k];
      adept::adouble fv = 0.5 * vMid * vMid + 9.81 * solh[k][0];
      for (unsigned i = 0; i < nDofv; i++) {
        if(!bdcv[k][i]) {
          for (unsigned j = 0; j < nDofh; j++) {
            double sign = ( i == j) ? -1. : 1;
            aResv[k][i] += sign * fv / dx;
          }
        }
      }
//      for (unsigned i = 0; i < nDofhe; i++){
// 	if(!bdche[k][i]){
// 	  if( i == 0){
// 	    aReshe[k][i] += -solhe[k][1] * solv[k][1] / ( 2 * dx) ;
// 	  }
// 	  else if (i == 1){
// 	    aReshe[k][i] += solhe[k][0] * solv[k][0] / ( 2 * dx) ;
// 	  }
// 	}
//      }
//       for (unsigned i = 0; i < nDofhe; i++) {
//         if(!bdche[k][i]) {
//           aReshe[k][i] = dx;
//           for (unsigned j = 0; j < nDofhe; j++) {
//             double sign = ( i == j) ? -1. : 1;
//             aReshe[k][i] += sign * 1000000 *  solhe[k][j] / (dx * dx);
//           }
//         }
//       }
    }


    vector< double > Res(NLayers * nDofs); // local redidual vector


    unsigned counter = 0;
    for(unsigned k = 0; k < NLayers; k++) {
      for(int i = 0; i < nDofh; i++) {
        Res[counter] =  aResh[k][i].value();
        //std::cout << Res[counter] << " " << std::flush;
        counter++;
      }
      for(int i = 0; i < nDofv; i++) {
        Res[counter] =  aResv[k][i].value();
        //std::cout << Res[counter] << " " << std::flush;
        counter++;
      }
//       for(int i = 0; i < nDofhe; i++) {
//         Res[counter] =  aReshe[k][i].value();
//         std::cout << Res[counter] << " " << std::flush;
//         counter++;
//       }
    }

    RES->add_vector_blocked(Res, l2GMap);


    for(unsigned k = 0; k < NLayers; k++) {
      // define the dependent variables
      s.dependent(&aResh[k][0], nDofh);
      s.dependent(&aResv[k][0], nDofv);
      //s.dependent(&aReshe[k][0], nDofhe);

      // define the independent variables
      s.independent(&solh[k][0], nDofh);
      s.independent(&solv[k][0], nDofv);
      //s.independent(&solhe[k][0], nDofhe);
    }

    // get the jacobian matrix (ordered by row major )
    vector < double > Jac(NLayers * nDofs * NLayers * nDofs);
    s.jacobian(&Jac[0], true);

    //store K in the global matrix KK
    KK->add_matrix_blocked(Jac, l2GMap, l2GMap);

    s.clear_independents();
    s.clear_dependents();

  }

  RES->close();
  KK->close();

//   PetscViewer    viewer;
//   PetscViewerDrawOpen(PETSC_COMM_WORLD,NULL,NULL,0,0,900,900,&viewer);
//   PetscObjectSetName((PetscObject)viewer,"FSI matrix");
//   PetscViewerPushFormat(viewer,PETSC_VIEWER_DRAW_LG);
//   MatView((static_cast<PetscMatrix*>(KK))->mat(),viewer);
//   double a;
//   std::cin>>a;
//
  
//  abort();
  MFN mfn;
  Mat A = (static_cast<PetscMatrix*>(KK))->mat();
  FN f, f1, f2, f3 , f4;
  
  double dt = dx / maxWaveSpeed * 10;
  
  std::cout << "AAAAAAAAAA " << dt << " "<< dx << " "<<maxWaveSpeed << std::endl;
  
  //dt = 100.;

  Vec v = (static_cast< PetscVector* >(RES))->vec();
  Vec y = (static_cast< PetscVector* >(EPS))->vec();

  MFNCreate( PETSC_COMM_WORLD, &mfn );

  MFNSetOperator( mfn, A );
  MFNGetFN( mfn, &f );


  FNCreate(PETSC_COMM_WORLD, &f1);
  FNCreate(PETSC_COMM_WORLD, &f2);
  FNCreate(PETSC_COMM_WORLD, &f3);
  FNCreate(PETSC_COMM_WORLD, &f4);

  FNSetType(f1, FNEXP);

  FNSetType(f2, FNRATIONAL);
  double coeff1[1] = { -1};
  FNRationalSetNumerator(f2, 1, coeff1);
  FNRationalSetDenominator(f2, 0, PETSC_NULL);

  FNSetType( f3, FNCOMBINE );

  FNCombineSetChildren(f3, FN_COMBINE_ADD, f1, f2);

  FNSetType(f4, FNRATIONAL);
  double coeff2[2] = {1., 0.};
  FNRationalSetNumerator(f4, 2, coeff2);
  FNRationalSetDenominator(f4, 0, PETSC_NULL);

  FNSetType( f, FNCOMBINE );

  FNCombineSetChildren(f, FN_COMBINE_DIVIDE, f3, f4);

  //FNPhiSetIndex(f,0);
  //FNSetType( f, FNPHI );
  //FNView(f,PETSC_VIEWER_STDOUT_WORLD);

  FNSetScale( f, dt, dt);
  MFNSetFromOptions( mfn );

  MFNSolve( mfn, v, y);
  MFNDestroy( &mfn );

  FNDestroy(&f1);
  FNDestroy(&f2);
  FNDestroy(&f3);
  FNDestroy(&f4);

  sol->UpdateSol(mlPdeSys->GetSolPdeIndex(), EPS, pdeSys->KKoffset);



}




