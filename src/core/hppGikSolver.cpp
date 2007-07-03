#include <time.h>
#include <sys/time.h>
#include "boost/numeric/ublas/vector_proxy.hpp"
#include "boost/numeric/ublas/matrix_proxy.hpp"
#include <boost/numeric/ublas/io.hpp>
#include <boost/numeric/bindings/traits/ublas_matrix.hpp>
#include <boost/numeric/bindings/lapack/gesvd.hpp>
#include "core/hppGikSolver.h"
#include "hppGikTools.h"

namespace lapack = boost::numeric::bindings::lapack;

using namespace ublas;

ChppGikSolver::ChppGikSolver(CjrlHumanoidDynamicRobot* inRobot)
{
    attRobot = inRobot;

    numDof = attRobot->numberDof();
    numJoints = numDof-6;

    xDefaultDim = 6;
    SVDThreshold = 0.001;
    ErrorThresh = 1e-8;

    LongSize = numJoints;
    scalar_vector<char> temp(numJoints,1);
    PIWeights = temp;
    PIWeightsBackup = temp;
    Weights = temp;


    DeltaQ.resize(numJoints,false);
    UsedIndexes.resize(numJoints,false);
    for (unsigned int i=0; i<numJoints;i++)
        UsedIndexes(i) = i;


    identity_matrix<double> tempid(numJoints);
    IdentityMat.resize(numJoints,numJoints,false);
    IdentityMat = tempid;
    BigMat1.resize(numJoints,numJoints,false);
    BigMat2.resize(numJoints,numJoints,false);

    NullSpace.resize(numJoints,numJoints,false);
    NullSpace = tempid;

    resizeMatrices(xDefaultDim);

    CurFullConfig.resize(numDof,false);

    BaseEuler.resize(3,false);

    H0.resize(4,4,false);
    Hf.resize(4,4,false);
    Hif.resize(4,4,false);
    InvHf.resize(4,4,false);

    prepareBrakeWindow();

    jobU = 'N';
    jobVt = 'A';
}


void ChppGikSolver::resizeMatrices(unsigned int inSize)
{
    xDefaultDim = inSize;

    CarvedJacobian.resize(xDefaultDim,numJoints,false);
    HatJacobian.resize(xDefaultDim,numJoints,false);
    Residual.resize(xDefaultDim,false);
    WJt.resize(numJoints,xDefaultDim,false);
    JWJt.resize(xDefaultDim,xDefaultDim,false);
    Jsharp.resize(numJoints,xDefaultDim,false);
    InverseJWJt.resize(xDefaultDim,xDefaultDim,false);

    PenroseMask.resize(xDefaultDim,false);

}


void ChppGikSolver::prepareBrakeWindow()
{
    BrakingZone = 0.12;
    WindowStep = 1e-3;

    unsigned int extra = 10;
    unsigned int sizeWindow = ChppGikTools::timetoRank(0.0,1.0,WindowStep)+1 ;

    JointUpperLimitWindow.resize(sizeWindow,false);

    ChppGikTools::minJerkCurve(1.0-WindowStep*extra,WindowStep,1.0,0.0,0.0,0.0,JointUpperLimitWindow);

    subrange(JointUpperLimitWindow, sizeWindow - extra, sizeWindow) = zero_vector<double>(extra);

    /* // dump braking window
    matrixNxP mat(1,sizeWindow);
    row(mat,0) = JointUpperLimitWindow;
    ChppGikTools::dumpMatrix( "cloche", mat, 0.0, WindowStep);
    */
}


double ChppGikSolver::brakeCoefForJoint(const double& qVal,const double& lowerLimit, const double& upperLimit, const double& dq)
{

    if (upperLimit ==lowerLimit)
        return 0;

    double qFraction = (qVal-lowerLimit)/(upperLimit-lowerLimit);


    if ((dq>0) && (qFraction > 1 - BrakingZone))
    {
        double curve_Fraction = (qFraction + BrakingZone - 1)/BrakingZone;
        unsigned int windowTick = ChppGikTools::timetoRank( 0.0, curve_Fraction, WindowStep);
        if (windowTick >= JointUpperLimitWindow.size())
            return 0;

        //std::cout << "up brake: "<< JointUpperLimitWindow(windowTick) <<"\n";

        return JointUpperLimitWindow(windowTick);
    }

    if ((dq<0) && (qFraction < BrakingZone))
    {
        double curve_Fraction = qFraction/BrakingZone;
        unsigned int windowTick = ChppGikTools::timetoRank( 0.0, curve_Fraction, WindowStep);
        //std::cout << "low brake: "<< JointUpperLimitWindow(JointUpperLimitWindow.size()-1-windowTick) <<"\n";
        return JointUpperLimitWindow(JointUpperLimitWindow.size()-1-windowTick);
    }

    return 1;
}

bool ChppGikSolver::weights(vectorN& inWeights)
{
    if (inWeights.size() == numJoints)
    {
        Weights = inWeights;
        return true;
    }
    else
        return false;
}


void ChppGikSolver::accountForJointLimits()
{
    LongSize = 0;
    unsigned int realrank;
    double coefLjoint;

    for (unsigned int i=0; i<numJoints;i++)
        if (Weights(i)>1e-2)
        {
            realrank = 6 + i;

            coefLjoint =  brakeCoefForJoint(attRobot->currentConfiguration()(realrank),attRobot->lowerBoundDof(realrank),attRobot->upperBoundDof(realrank),attRobot->currentVelocity()(realrank));

            UsedIndexes(LongSize) = i;

            PIWeightsBackup(LongSize) = Weights(i) * coefLjoint;

            LongSize++;
        }
}


bool ChppGikSolver::gradientStep(std::vector<CjrlGikStateConstraint*>& inSortedConstraints)
{
    accountForJointLimits();
    
    subrange(PIWeights,0,LongSize) = subrange(PIWeightsBackup,0,LongSize);

    //store the fixed foot's tranformation (Hif)
    FixedJoint = &(attRobot->fixedJoint(0));
    if (FixedJoint == 0)
    {
        std::cout << "solve(): the robot did not have a valid fixed joint.\n";
        return false;
    }
    //joints from root to fixed joint (including both)
    std::vector<CjrlJoint*> supportJoints = FixedJoint->jointsFromRootToThis();
    ChppGikTools::Matrix4toUblas(FixedJoint->currentTransformation(),Hif);

    unsigned int elementCount;
    bool recompute = true;
    Iteration = 0;
    std::vector<CjrlGikStateConstraint*>::iterator iter;
    //while maxiteration not reached
    while  (recompute)
    {
        elementCount = 0;
        subrange(NullSpace,0,LongSize,0,LongSize) = subrange(IdentityMat,0,LongSize,0,LongSize);
        DeltaQ.clear();

        //for every subtask
        for (iter = inSortedConstraints.begin(); iter != inSortedConstraints.end(); iter++)
        {
            elementCount++;

            //determin task dimension
            xDim = (*iter)->dimension();

            if (xDim > xDefaultDim)
                resizeMatrices(xDim);

            //store used columns only
            for (unsigned int col = 0; col<LongSize;col++)
                subrange(CarvedJacobian,0,xDim,col,col+1) =  subrange((*iter)->jacobian(),0,xDim,UsedIndexes(col),UsedIndexes(col)+1);

            //update value according to previous tasks
            noalias(subrange(Residual,0,xDim)) = (*iter)->value();
            noalias(subrange(Residual,0,xDim)) -= prod(subrange(CarvedJacobian,0,xDim,0,LongSize),subrange(DeltaQ,0,LongSize));

            //slightly faster product to obtain projected jacobian on previous tasks nullspace
            HatJacobian.clear();
            vectorN& ElementMask = (*iter)->influencingDofs();
            for (unsigned int d = 0; d<xDim;d++)
            {
                for (unsigned int col = 0; col<LongSize;col++)
                    if (ElementMask(UsedIndexes(col)) == 1)
                        noalias(subrange(HatJacobian,d,d+1,0,LongSize)) += CarvedJacobian(d,col) * subrange(NullSpace,col,col+1,0,LongSize);
            }

            //pseudo inverse of the projected jacobian
            noalias(subrange(WJt,0,LongSize,0,xDim)) = trans(subrange(HatJacobian,0,xDim,0,LongSize));

            for (unsigned int lin=0;lin<LongSize;lin++)
                row(WJt,lin) *= PIWeights(lin);

            noalias(subrange(JWJt,0,xDim,0,xDim)) = prod(subrange(HatJacobian,0,xDim,0,LongSize),subrange(WJt,0,LongSize,0,xDim));

            //svd
            vectorN tempS(xDim);
            matrix<double, column_major> tempU(xDim,xDim);
            matrix<double, column_major> tempVt(xDim,xDim);
            matrix<double, column_major> tempJWJt = subrange(JWJt,0,xDim,0,xDim);
            lapack::gesvd(jobU,jobVt,tempJWJt, tempS, tempU, tempVt);
            tempU = trans(tempVt);

            PenroseMask.clear();

            //Determin task value projection space
            for (unsigned int i=0;i<xDim;i++)
            {
                if (SVDThreshold > tempS(i))
                    row(tempVt,i) *= 0;
                else
                {
                    PenroseMask(i) =  1;
                    row(tempVt,i) /= tempS(i);
                }
            }

            if (PenroseMask(0)==0)
                continue;

            //inverse of JwJt
            noalias(subrange(InverseJWJt,0,xDim,0,xDim)) = prod(tempU,tempVt);

            //PseudoInverse Jsharp
            noalias(subrange(Jsharp,0,LongSize,0,xDim)) = prod(subrange(WJt,0,LongSize,0,xDim),subrange(InverseJWJt,0,xDim,0,xDim));

            //Updated deltaQ
            noalias(subrange(DeltaQ,0,LongSize)) += prod(subrange(Jsharp,0,LongSize,0,xDim), subrange(Residual,0,xDim));

            //Updated null space
            noalias(subrange(BigMat2,0,LongSize,0,LongSize)) = prod(subrange(Jsharp,0,LongSize,0,xDim),subrange(HatJacobian,0,xDim,0,LongSize));
            noalias(subrange(BigMat1,0,LongSize,0,LongSize)) = prod(subrange(NullSpace,0,LongSize,0,LongSize),  subrange(BigMat2,0,LongSize,0,LongSize) );
            subrange(NullSpace,0,LongSize,0,LongSize).minus_assign(subrange(BigMat1,0,LongSize,0,LongSize));
        }

        //compute new dof vector & perform a basic check on joint limits
        CurFullConfig = attRobot->currentConfiguration();

        //joint limit checking
        unsigned int iC,realIndex;
        recompute = false;
        bool atLeastOneDofHasChanged = false;
        for (iC=0; iC< LongSize; iC++)
        {
            //update dof config
            realIndex = 6+UsedIndexes(iC);
            CurFullConfig(realIndex) += DeltaQ(iC);

            //check for joint limits
            if (CurFullConfig(realIndex) < attRobot->lowerBoundDof(realIndex)+1e-2 || CurFullConfig(realIndex) > attRobot->upperBoundDof(realIndex)-1e-2)
            {
                recompute = true;
                PIWeights(iC) = 0; // disable this dof for recomputation
            }
            else
                atLeastOneDofHasChanged = true;
        }
        if (recompute)
            if (!atLeastOneDofHasChanged)
                break;
            else
                continue;

        //go to waist frame
        for (iC=0; iC< 6; iC++)
            CurFullConfig(iC) = 0;

        //update joints transformations from root to fixed joint in waist frame
        for (iC=0; iC< supportJoints.size(); iC++)
            supportJoints[iC]->updateTransformation(CurFullConfig);

        //Compute new waist transformation
        ChppGikTools::Matrix4toUblas(FixedJoint->currentTransformation(),Hf);
        ChppGikTools::invertTransformation(Hf,InvHf);
        noalias(H0) = prod(Hif, InvHf);

        //Compute free flyer dofs (apparently the dynamic robot require euler XYZ)
        ChppGikTools::RottoEulerZYX(subrange(H0,0,3,0,3),BaseEuler);
        for (iC=0; iC< 3; iC++)
            CurFullConfig(iC) = H0(iC,3);
        for (iC=3; iC< 6; iC++)
            CurFullConfig(iC) = BaseEuler(iC-3);

        attRobot->applyConfiguration(CurFullConfig);
    }

    return true;
}


ChppGikSolver::~ChppGikSolver()
{}

