/*=========================================================================

  Program:   ORFEO Toolbox
  Language:  C++
  Date:      $Date$
  Version:   $Revision$


  Copyright (c) Centre National d'Etudes Spatiales. All rights reserved.
  See OTBCopyright.txt for details.


  This software is distributed WITHOUT ANY WARRANTY; without even
  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#include "itkMacro.h"
#include "otbMacro.h"

#include "otbSpectralResponse.h"
#include "otbSatelliteRSR.h"
#include "otbReduceSpectralResponse.h"

int otbReduceSpectralResponseNew(int argc, char * argv[])
{
  typedef otb::SpectralResponse< double, double>  ResponseType;
  //typedef ResponseType::Pointer  ResponsePointerType;
  
  typedef otb::SatelliteRSR< double, double>  SatRSRType;
  //typedef SatRSRType::Pointer  SatRSRPointerType;
  
  typedef otb::ReduceSpectralResponse < ResponseType, SatRSRType>  ReduceResponseType;
  typedef ReduceResponseType::Pointer  ReduceResponseTypePointerType;
  //Instantiation
  ReduceResponseTypePointerType  myResponse=ReduceResponseType::New();

  return EXIT_SUCCESS;
}
