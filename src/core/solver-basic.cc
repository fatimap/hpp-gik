#include "boost/numeric/ublas/vector_proxy.hpp"
#include "boost/numeric/ublas/matrix_proxy.hpp"
#include "boost/numeric/ublas/operation.hpp"
#include <boost/numeric/ublas/io.hpp>
#include <boost/numeric/bindings/traits/ublas_matrix.hpp>
#include <boost/numeric/bindings/lapack/gesvd.hpp>
#include "hpp/gik/core/solver-basic.hh"
#include "hpp/gik/constraint/configuration-constraint.hh"
#include "hpp/gik/core/motion-plan-element.hh"
#include "hpp/gik/tools.hh"

namespace lapack = boost::numeric::bindings::lapack;
using namespace boost::numeric::ublas;


ChppGikSolverBasic::ChppGikSolverBasic(unsigned int numberParam)
{
    attNumberParam = numberParam;
    xDefaultDim = 6;
    attSVDThreshold = 0.001;
    LongSize =  attNumberParam;
    ElementMask = PIWeights = Weights = scalar_vector<double> ( attNumberParam,1 );
    UsedIndexes.resize ( attNumberParam,false );
    for ( unsigned int i=0; i<attNumberParam;i++ )
        UsedIndexes ( i ) = i;
    identity_matrix<double> tempid ( attNumberParam );
    IdentityMat.resize ( attNumberParam,attNumberParam,false );
    IdentityMat = tempid;
    attSolution.resize(attNumberParam);
    resizeMatrices ( xDefaultDim );
    resetSolution();
    deltaqComplete.resize(attNumberParam);
    valueComplete.resize(attNumberParam);
    jobU = 'N';
    jobVt = 'A';
}

bool ChppGikSolverBasic::weights ( vectorN& inWeights )
{
    if ( inWeights.size() == attNumberParam )
    {
        Weights = inWeights;
        return (resetSolution()!=0);
    }
    else
    {
        std::cout << "ChppGikSolverBasic::weights() incorrect size"<<std::endl;
        return false;
    }
}


void ChppGikSolverBasic::resizeMatrices ( unsigned int inSize)
{
    Residual.resize ( inSize,false );
    PenroseMask.resize ( inSize,false );

    CarvedJacobian.resize ( inSize,LongSize,false );
    HatJacobian.resize ( inSize,LongSize,false );
    WJt.resize ( LongSize,inSize,false );
    JWJt.resize ( inSize,inSize,false );
    Jsharp.resize ( LongSize,inSize,false );
    InverseJWJt.resize ( inSize,inSize,false );

    tempU.resize(inSize,inSize,false);
    tempVt.resize(inSize,inSize,false);
    tempS.resize(inSize, false);
}



bool ChppGikSolverBasic::setActiveParameters(const vectorN& inMask)
{
    if (inMask.size() != attNumberParam)
    {
        std::cout << "ChppGikSolverBasic::setActiveParameters() incorrect size"<<std::endl;
        return false;
    }
    ElementMask = inMask;
    return true;
}

unsigned int ChppGikSolverBasic::resetSolution()
{
    LongSize = 0;
    for ( unsigned int i=0; i<attNumberParam;i++ )
        if ( Weights ( i ) >1e-2 )
        {
            UsedIndexes ( LongSize ) = i;
            PIWeights ( LongSize ) = Weights ( i );
            LongSize++;
        }

    NullSpace.resize ( LongSize, LongSize,false );
    NullSpace = subrange ( IdentityMat,0,LongSize,0,LongSize );
    DeltaQ.resize ( LongSize,false );
    DeltaQ.clear();
    return LongSize;
}

bool ChppGikSolverBasic::solveTask(CjrlGikStateConstraint *inConstraint, double inSRcoef, bool inComputeHatJacobian , bool inComputeNullspace )
{
    if (inConstraint->jacobian().size2()!=attNumberParam)
    {
        std::cout << "ChppGikSolverBasic::solveTask() err: given constraint's jacobian size did not match number of parameters of this solver."<<std::endl;
        return false;
    }
    //determin task dimension
    xDim = inConstraint->dimension();

    bool specialCase = false;
    ChppGikConfigurationConstraint* confc = dynamic_cast<ChppGikConfigurationConstraint*>(inConstraint);

    if (!confc)
    {
        ChppGikMotionPlanElement* cmpConfc = dynamic_cast<ChppGikMotionPlanElement*>(inConstraint);
        if (cmpConfc  && (cmpConfc->constraints().size() == 1))
        {
            confc =  dynamic_cast<ChppGikConfigurationConstraint*>(cmpConfc->constraints()[0]);
            if (confc)
                specialCase = true;
        }
    }
    else
        specialCase = true;
 
    if (specialCase)
    {
        Residual.resize ( LongSize,false );
        deltaqComplete.clear();
        valueComplete.clear();
        for ( unsigned int iC=0; iC< LongSize; iC++ )
            deltaqComplete ( UsedIndexes ( iC ) ) = DeltaQ ( iC );

        unsigned int valInd = 0,li;
        for ( li = 0; (li<attNumberParam) && (valInd <xDim);li++ )
        {
            if (inConstraint->influencingDofs()(li)==1)
            {
                valueComplete(li) = -inConstraint->value()(valInd);
                valInd++;
            }
            else
                valueComplete(li) = deltaqComplete(li);
        }
        valueComplete.minus_assign(deltaqComplete);
        valInd = 0;
        for ( li = 0; (li<attNumberParam) && (valInd <LongSize);li++ )
        {
            if ((Weights(li)>1e-2))
            {
                Residual(valInd) = valueComplete(li);
                valInd++;
            }
        }
        noalias ( DeltaQ ) += prod ( NullSpace, Residual );
        if (inComputeNullspace)
        {
            zero_vector<double> zeroV(LongSize);
            unsigned int realInd;
            for ( unsigned int col = 0; col<LongSize;col++ )
            {
                realInd = UsedIndexes ( col );
                if ( ElementMask ( realInd ) == 1)
                    column(NullSpace,col) = zeroV;
            }
        }
    }
    else
    {
        //resize matrices
        resizeMatrices ( xDim );
        //store used columns only
        for ( unsigned int col = 0; col<LongSize;col++ )
            noalias (column ( CarvedJacobian,col) )=  column ( inConstraint->jacobian(),UsedIndexes ( col ));
        //update value according to previous tasks
        noalias (Residual) = -inConstraint->value();
        noalias (Residual) -= prod (CarvedJacobian,DeltaQ);
        HatJacobian.clear();
        if (inComputeHatJacobian)
            for ( unsigned int d = 0; d<xDim;d++ )
            {
                for ( unsigned int col = 0; col<LongSize;col++ )
                    if ( ElementMask ( UsedIndexes ( col ) ) == 1)
                        noalias ( row (HatJacobian,d) ) += CarvedJacobian ( d,col ) * row ( NullSpace,col);
            }
        else
            noalias (HatJacobian) = CarvedJacobian;

        noalias ( WJt ) = trans ( HatJacobian);
        for ( unsigned int lin=0;lin<LongSize;lin++ )
            row ( WJt,lin ) *= PIWeights ( lin );

        noalias ( JWJt ) = prod ( HatJacobian,  WJt );

        //svd
        if (inSRcoef != 0)
        {
            for ( unsigned int i=0;i<xDim;i++ )
                JWJt(i,i) += inSRcoef;
        }

        lapack::gesvd ( jobU, jobVt, JWJt, tempS, tempU, tempVt );
        tempU = trans ( tempVt );
        PenroseMask.clear();
        //Determin task value projection space
        for ( unsigned int i=0;i<xDim;i++ )
        {
            if ( attSVDThreshold > tempS ( i ) )
                row ( tempVt,i ) *= 0;
            else
            {
                PenroseMask ( i ) =  1;
                row ( tempVt,i ) /= tempS ( i );
            }
        }
        if ( PenroseMask ( 0 ) ==0 )
            return true;

        noalias ( InverseJWJt ) = prod ( tempU,tempVt );
        noalias ( Jsharp ) = prod (WJt , InverseJWJt);
        noalias ( DeltaQ ) += prod ( Jsharp, Residual );
        if (inComputeNullspace)
        {
            noalias (  NullSpace ) -= prod ( Jsharp, HatJacobian );
        }
    }
    return true;
}

const vectorN& ChppGikSolverBasic::solution()
{
    attSolution.clear();

    for ( unsigned int iC=0; iC< LongSize; iC++ )
    {
        attSolution ( UsedIndexes ( iC ) ) = DeltaQ ( iC );
    }

    return attSolution;
}

void ChppGikSolverBasic::SVDThreshold(double i_threshold)
{
    attSVDThreshold = i_threshold;
}

const vectorN& ChppGikSolverBasic::penroseMask() const
{
    return PenroseMask;
}

ChppGikSolverBasic::~ChppGikSolverBasic()
{}
