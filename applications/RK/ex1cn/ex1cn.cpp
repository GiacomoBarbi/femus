/** tutorial/Ex14 
 * This example shows how to set and solve the weak form of the Bistable Equation 
 *          $$ \dfrac{\partial u}{ \partial t}-\epsilon \nabla \cdot u=u-u^3  \text{in} \Omega $$
 *          $$ \nabla u.n=0 \text{ on} \partial \Omega $$
 *          $$ u= u_{0} in \Omega x {t=0} $$
 * on a square domain \Omega=[-1,1]x[-1,1]. 
 * all the coarse-level meshes are removed;
 * a multilevel problem and an equation system are initialized;
 * a direct solver is used to solve the problem.
 **/

#include "FemusInit.hpp"
#include "MultiLevelSolution.hpp"
#include "MultiLevelProblem.hpp"
#include "NumericVector.hpp"
#include "VTKWriter.hpp"
#include "TransientSystem.hpp"
#include "NonLinearImplicitSystem.hpp"
#include "adept.h"


using namespace femus;


double GetTimeStep(const double time) {
  double dt = 0.1;
  return dt;
}

bool SetBoundaryCondition(const std::vector < double >& x, const char solName[], double& value, const int faceIndex, const double time) {
  
  bool dirichlet = true; //Neumann
  double pi=acos(-1);
  
  //value = cos( sqrt(2. * pi) * x[0] ) * cos( sqrt(2. * pi) * x[1] ) * exp(-2. * pi * time);
  value = cos(2*pi*x[0]*x[0])*cos(2*pi*x[1]*x[1]) * exp(-time);  
    
//   bool dirichlet = false; //Neumann
//   value = 0.;

  return dirichlet;
}

double InitalValue(const std::vector < double >& x) {
  double pi=acos(-1);  
  return cos(2*pi*x[0]*x[0])*cos(2*pi*x[1]*x[1]); 
  //return cos( sqrt(2. * pi) * x[0] ) * cos( sqrt(2. * pi) * x[1] );
}


void AssembleAllanChanProblem_AD(MultiLevelProblem& ml_prob);

std::pair < double, double > GetErrorNorm(MultiLevelSolution* mlSol);

int main(int argc, char** args) {

  // init Petsc-MPI communicator
  FemusInit mpinit(argc, args, MPI_COMM_WORLD);

  // define MultiLevel object "mlMsh". 
  MultiLevelMesh mlMsh;
  // read coarse level mesh and generate finers level meshes
  double scalingFactor = 1.;
  mlMsh.ReadCoarseMesh("./input/square_quad.neu", "seventh", scalingFactor);
  //mlMsh.ReadCoarseMesh("./input/cube_tet.neu", "seventh", scalingFactor);
  /* "seventh" is the order of accuracy that is used in the gauss integration scheme
    probably in future it is not going to be an argument of this function   */
  unsigned dim = mlMsh.GetDimension(); // Domain dimension of the problem.
  unsigned maxNumberOfMeshes; // The number of mesh levels.

  unsigned numberOfUniformLevels = 5; //We apply uniform refinement.
  unsigned numberOfSelectiveLevels = 0; // We may want to see the solution on some levels.
  mlMsh.RefineMesh(numberOfUniformLevels , numberOfUniformLevels + numberOfSelectiveLevels, NULL);

    // erase all the coarse mesh levels
  mlMsh.EraseCoarseLevels(numberOfUniformLevels - 1); // We check the solution on the finest mesh.

    // print mesh info
  mlMsh.PrintInfo();

  
  // define the multilevel solution and attach the mlMsh object to it
  MultiLevelSolution mlSol(&mlMsh); // Here we provide the mesh info to the problem.

  // add variables to mlSol
  mlSol.AddSolution("u", LAGRANGE, SECOND, 2); // We may have more than one, add each of them as u,v,w with their apprx type.
  mlSol.Initialize("u", InitalValue); // Since this is time depend problem.

  // attach the boundary condition function and generate boundary data
  mlSol.AttachSetBoundaryConditionFunction(SetBoundaryCondition);
  mlSol.GenerateBdc("u", "Time_dependent");

  // define the multilevel problem attach the mlSol object to it
  MultiLevelProblem mlProb(&mlSol); //

  // add system Poisson in mlProb as a Non Linear Implicit System
  TransientNonlinearImplicitSystem & system = mlProb.add_system < TransientNonlinearImplicitSystem > ("AllanChan");

  // add solution "u" to system
  system.AddSolutionToSystemPDE("u");

  // attach the assembling function to system
  system.SetAssembleFunction(AssembleAllanChanProblem_AD);

  // time loop parameter
  system.AttachGetTimeIntervalFunction(GetTimeStep);
  const unsigned int n_timesteps = 250;

  
  system.init();
  
   // ******* Print solution *******
  mlSol.SetWriter(VTK);
  mlSol.GetWriter()->SetGraphVariable ("u");
  mlSol.GetWriter()->SetDebugOutput(false);

  std::vector<std::string> print_vars;
  print_vars.push_back("All");
  
  mlSol.GetWriter()->Write(DEFAULT_OUTPUTDIR,"biquadratic",print_vars, 0);


  for (unsigned time_step = 0; time_step < n_timesteps; time_step++) {

    system.CopySolutionToOldSolution();

    system.MGsolve();

    mlSol.GetWriter()->Write(DEFAULT_OUTPUTDIR,"biquadratic",print_vars, time_step+1);
  }
  
  mlProb.clear();
  
  return 0;
}



/**
 * This function assemble the stiffnes matrix KK and the residual vector Res
 * Using automatic differentiation for Newton iterative scheme
 *                  J(u0) w =  - F(u0)  ,
 *                  with u = u0 + w
 *                  - F = f(x) - J u = Res
 *                  J = \grad_u F
 *
 * thus
 *                  J w = f(x) - J u0
 **/
void AssembleAllanChanProblem_AD(MultiLevelProblem& ml_prob) {
  //  ml_prob is the global object from/to where get/set all the data
  //  level is the level of the PDE system to be assembled
  //  levelMax is the Maximum level of the MultiLevelProblem
  //  assembleMatrix is a flag that tells if only the residual or also the matrix should be assembled

  // call the adept stack object


  adept::Stack& s = FemusInit::_adeptStack;

  //  extract pointers to the several objects that we are going to use

  TransientNonlinearImplicitSystem* mlPdeSys  = &ml_prob.get_system<TransientNonlinearImplicitSystem> ("AllanChan");   // pointer to the linear implicit system named "Poisson"
  const unsigned level = mlPdeSys->GetLevelToAssemble(); // We have different level of meshes. we assemble the problem on the specified one.

  Mesh*                    msh = ml_prob._ml_msh->GetLevel(level);    // pointer to the mesh (level) object
  elem*                     el = msh->el;  // pointer to the elem object in msh (level)

  MultiLevelSolution*    mlSol = ml_prob._ml_sol;  // pointer to the multilevel solution object
  Solution*                sol = ml_prob._ml_sol->GetSolutionLevel(level);    // pointer to the solution (level) object

  LinearEquationSolver* pdeSys = mlPdeSys->_LinSolver[level]; // pointer to the equation (level) object
  SparseMatrix*             KK = pdeSys->_KK;  // pointer to the global stifness matrix object in pdeSys (level)
  NumericVector*           RES = pdeSys->_RES; // pointer to the global residual vector object in pdeSys (level)

  const unsigned  dim = msh->GetDimension(); // get the domain dimension of the problem
  unsigned dim2 = (3 * (dim - 1) + !(dim - 1));        // dim2 is the number of second order partial derivatives (1,3,6 depending on the dimension)
  const unsigned maxSize = static_cast< unsigned >(ceil(pow(3, dim))); // Return a value of unsigned // conservative: based on line3, quad9, hex27

  unsigned    iproc = msh->processor_id(); // get the process_id (for parallel computation)

  //solution variable
  unsigned soluIndex;
  soluIndex = mlSol->GetIndex("u");    // get the position of "u" in the ml_sol object
  unsigned soluType = mlSol->GetSolutionType(soluIndex);    // get the finite element type for "u"

  unsigned soluPdeIndex;
  soluPdeIndex = mlPdeSys->GetSolPdeIndex("u");    // get the position of "u" in the pdeSys object

  vector < adept::adouble >  solu; // local solution
  solu.reserve(maxSize);

  vector < double >  soluOld; // local solution
  soluOld.reserve(maxSize);

  
  vector < vector < double > > x(dim);    // local coordinates. x is now dim x m matrix.
  unsigned xType = 2; // get the finite element type for "x", it is always 2 (LAGRANGE QUADRATIC)

  for (unsigned k = 0; k < dim; k++) { 
    x[k].reserve(maxSize); // dim x maxsize is reserved for x.  
  }

  vector <double> phi;  // local test function
  vector <double> phi_x; // local test function first order partial derivatives
  
  double weight; // gauss point weight
  phi.reserve(maxSize);
  phi_x.reserve(maxSize * dim); // This is probably gradient but he is doing the life difficult for me!
  
  vector< adept::adouble > aRes; // local redidual vector
  aRes.reserve(maxSize);

  vector< int > l2GMap; // local to global mapping
  l2GMap.reserve(maxSize);
  vector< double > Res; // local redidual vector
  Res.reserve(maxSize);
  vector < double > Jac;
  Jac.reserve(maxSize * maxSize);

  KK->zero(); // Set to zero all the entries of the Global Matrix
  RES->zero(); // Set to zero all the entries of the Global Residual

  // element loop: each process loops only on the elements that owns
  // Adventure starts here!
  
  double dt = GetTimeStep(0.); 
  
  for (int iel = msh->_elementOffset[iproc]; iel < msh->_elementOffset[iproc + 1]; iel++) {
     
    short unsigned ielGeom = msh->GetElementType(iel);
    unsigned nDofu  = msh->GetElementDofNumber(iel, soluType);    // number of solution element dofs
    unsigned nDofx = msh->GetElementDofNumber(iel, xType);    // number of coordinate element dofs

    // resize local arrays
    l2GMap.resize(nDofu);
    solu.resize(nDofu);
    soluOld.resize(nDofu);

    for (int k = 0; k < dim; k++) {
      x[k].resize(nDofx); // Now we 
    }

    aRes.resize(nDofu);    //resize
    std::fill(aRes.begin(), aRes.end(), 0);    //set aRes to zero

    // local storage of global mapping and solution
    for (unsigned i = 0; i < nDofu; i++) {
      unsigned solDof = msh->GetSolutionDof(i, iel, soluType);    // global to global mapping between solution node and solution dof
      solu[i] = (*sol->_Sol[soluIndex])(solDof);                  // global extraction and local storage for the solution
      soluOld[i] = (*sol->_SolOld[soluIndex])(solDof);            // global extraction and local storage for the solution
      l2GMap[i] = pdeSys->GetSystemDof(soluIndex, soluPdeIndex, i, iel);    // global to global mapping between solution node and pdeSys dof
    }

    // local storage of coordinates
    for (unsigned i = 0; i < nDofx; i++) {
      unsigned xDof  = msh->GetSolutionDof(i, iel, xType);    // global to global mapping between coordinates node and coordinate dof

      for (unsigned k = 0; k < dim; k++) {
        x[k][i] = (*msh->_topology->_Sol[k])(xDof);      // global extraction and local storage for the element coordinates
      }
    }


    // start a new recording of all the operations involving adept::adouble variables
    s.new_recording();
           
    // *** Face Gauss point loop (boundary Integral) ***
//     for ( unsigned jface = 0; jface < msh->GetElementFaceNumber ( iel ); jface++ ) {
//       int faceIndex = el->GetBoundaryIndex(iel, jface);
//       // look for boundary faces
//       if ( faceIndex == 1 ) {  
//         const unsigned faceGeom = msh->GetElementFaceType ( iel, jface );
//         unsigned faceDofs = msh->GetElementFaceDofNumber (iel, jface, soluType);
//                     
//         vector  < vector  <  double> > faceCoordinates ( dim ); // A matrix holding the face coordinates rowwise.
//         for ( int k = 0; k < dim; k++ ) {
//           faceCoordinates[k].resize (faceDofs);
//         }
//         for ( unsigned i = 0; i < faceDofs; i++ ) {
//           unsigned inode = msh->GetLocalFaceVertexIndex ( iel, jface, i ); // face-to-element local node mapping.
//           for ( unsigned k = 0; k < dim; k++ ) {
//             faceCoordinates[k][i] =  x[k][inode]; // We extract the local coordinates on the face from local coordinates on the element.
//           }
//         }
//         for ( unsigned ig = 0; ig  <  msh->_finiteElement[faceGeom][soluType]->GetGaussPointNumber(); ig++ ) { 
//             // We call the method GetGaussPointNumber from the object finiteElement in the mesh object msh. 
//           vector < double> normal;
//           msh->_finiteElement[faceGeom][soluType]->JacobianSur ( faceCoordinates, ig, weight, phi, phi_x, normal );
//             
//           adept::adouble solu_gss = 0;
//           double soluOld_gss = 0;
//       
//             
// 
//           for ( unsigned i = 0; i < faceDofs; i++ ) {
//             unsigned inode = msh->GetLocalFaceVertexIndex ( iel, jface, i ); // face-to-element local node mapping.
//             solu_gss += phi[i] * solu[inode];
//             soluOld_gss += phi[i] * soluOld[inode];
//           }
//           
//           // *** phi_i loop ***
//           double eps = 10; 
//           for ( unsigned i = 0; i < faceDofs; i++ ) {
//             unsigned inode = msh->GetLocalFaceVertexIndex ( iel, jface, i );
//             aRes[inode] +=  phi[i] * eps * 0.5 * (solu_gss + soluOld_gss) * weight;
//           }        
//         }
//       }
//     }   
    
    // *** Element Gauss point loop ***
    for (unsigned ig = 0; ig < msh->_finiteElement[ielGeom][soluType]->GetGaussPointNumber(); ig++) {
      // *** get gauss point weight, test function and test function partial derivatives ***
      msh->_finiteElement[ielGeom][soluType]->Jacobian(x, ig, weight, phi, phi_x);

      // evaluate the solution, the solution derivatives and the coordinates in the gauss point
      adept::adouble solu_gss = 0;
      vector < adept::adouble > gradSolu_gss(dim, 0.);
      
      double soluOld_gss = 0;
      vector < double > gradSoluOld_gss(dim, 0.);
            

      for (unsigned i = 0; i < nDofu; i++) {
        solu_gss += phi[i] * solu[i];
        soluOld_gss += phi[i] * soluOld[i];

        for (unsigned k = 0; k < dim; k++) {
          gradSolu_gss[k] += phi_x[i * dim + k] * solu[i];
          gradSoluOld_gss[k] += phi_x[i * dim + k] * soluOld[i];
        }
      }

      // *** phi_i loop ***
      for (unsigned i = 0; i < nDofu; i++) {

        adept::adouble graduGradphi = 0.;
        double graduOldGradphi = 0.;
        double eps=0.01;
        //double eps=1;

        for (unsigned k = 0; k < dim; k++) {
          graduGradphi   +=   phi_x[i * dim + k] * gradSolu_gss[k];
          graduOldGradphi   +=   phi_x[i * dim + k] * gradSoluOld_gss[k];
        }
             
        aRes[i] += ( (solu_gss - soluOld_gss) * phi[i] / dt +  eps*( graduGradphi ) - (solu_gss - solu_gss*solu_gss*solu_gss) * phi[i]  ) * weight;
        //aRes[i] += ( (solu_gss - soluOld_gss) * phi[i] / dt +  eps*( graduGradphi ) ) * weight;

      } // end phi_i loop
    } // end gauss point loop

    //--------------------------------------------------------------------------------------------------------
    // Add the local Matrix/Vector into the global Matrix/Vector

    //copy the value of the adept::adoube aRes in double Res and store
    Res.resize(nDofu);    //resize

    for (int i = 0; i < nDofu; i++) {
      Res[i] = - aRes[i].value();
    }

    RES->add_vector_blocked(Res, l2GMap);

    // define the dependent variables
    s.dependent(&aRes[0], nDofu);

    // define the independent variables
    s.independent(&solu[0], nDofu);

    // get the jacobian matrix (ordered by row major )
    Jac.resize(nDofu * nDofu);    //resize
    s.jacobian(&Jac[0], true);

    //store K in the global matrix KK
    KK->add_matrix_blocked(Jac, l2GMap, l2GMap);

    s.clear_independents();
    s.clear_dependents();

  } //end element loop for each process

  RES->close();

  KK->close();

  // ***************** END ASSEMBLY *******************
}

