/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
Files in this directory have been contributed by PLUMED users.
Please check below the actual author of this contribution.
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */

/*+++
When you add a contributed functionality, please write here the copyright notice
*/

#include "function/Function.h"
#include "core/ActionRegister.h"

#include <cmath>

using namespace std;

namespace PLMD{
namespace contrib{

//+PLUMEDOC FUNCTION TEST_CONTRIB_FUNCTION
/*
THIS IS A TEST FUNCTION
\par Examples

*/
//+ENDPLUMEDOC


class TestContribFunction :
  public function::Function
{
  bool normalize;
  std::vector<double> coefficients;
  std::vector<double> powers;
public:
  TestContribFunction(const ActionOptions&);
  void calculate();
  static void registerKeywords(Keywords& keys);
};


PLUMED_REGISTER_ACTION(TestContribFunction,"TEST_CONTRIB_FUNCTION")

void TestContribFunction::registerKeywords(Keywords& keys){
  Function::registerKeywords(keys);
  keys.use("ARG"); keys.use("PERIODIC");
  keys.add("compulsory","COEFFICIENTS","1.0","the coefficients of the arguments in your function");
  keys.add("compulsory","POWERS","1.0","the powers to which you are raising each of the arguments in your function");
  keys.addFlag("NORMALIZE",false,"normalize all the coefficents so that in total they are equal to one");
}

TestContribFunction::TestContribFunction(const ActionOptions&ao):
Action(ao),
Function(ao),
normalize(false),
coefficients(getNumberOfArguments(),1.0),
powers(getNumberOfArguments(),1.0)
{
  parseVector("COEFFICIENTS",coefficients);
  if(coefficients.size()!=static_cast<unsigned>(getNumberOfArguments()))
    error("Size of COEFFICIENTS array should be the same as number for arguments");

  parseVector("POWERS",powers);
  if(powers.size()!=static_cast<unsigned>(getNumberOfArguments()))
    error("Size of POWERS array should be the same as number for arguments");

  parseFlag("NORMALIZE",normalize);

  if(normalize){
    double n=0.0;
    for(unsigned i=0;i<coefficients.size();i++) n+=coefficients[i];
    for(unsigned i=0;i<coefficients.size();i++) coefficients[i]*=(1.0/n);
  }
 
  addValueWithDerivatives(); 
  checkRead();

  log.printf("  with coefficients:");
  for(unsigned i=0;i<coefficients.size();i++) log.printf(" %f",coefficients[i]);
  log.printf("\n");
  log.printf("  and powers:");
  for(unsigned i=0;i<powers.size();i++) log.printf(" %f",powers[i]);
  log.printf("\n");
}

void TestContribFunction::calculate(){
  double combine=0.0;
  for(unsigned i=0;i<coefficients.size();++i){
    combine+=coefficients[i]*pow(getArgument(i),powers[i]);
    setDerivative(i,coefficients[i]*powers[i]*pow(getArgument(i),powers[i]-1.0));
  };
  setValue(combine);
}

}
}


