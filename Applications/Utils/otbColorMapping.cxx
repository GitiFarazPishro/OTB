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
#include "otbWrapperApplication.h"
#include "otbWrapperApplicationFactory.h"


#include <fstream>
#include <map>
// Include differents method for color mapping
#include "otbChangeLabelImageFilter.h"
#include "itkLabelToRGBImageFilter.h"
#include "itkScalarToRGBColormapImageFilter.h"
#include "otbReliefColormapFunctor.h"

#include "itkImageRegionSplitter.h"
#include "otbStreamingTraits.h"

#include "otbRAMDrivenStrippedStreamingManager.h"
#include "otbStreamingShrinkImageFilter.h"
#include "itkListSample.h"
#include "otbListSampleToHistogramListGenerator.h"

#include "itkDenseFrequencyContainer.h"
#include "itkVariableLengthVector.h"
#include "itkHistogram.h"
#include "otbObjectList.h"
#include "otbVisualizationPixelTraits.h"

#include "itkImageRegionConstIterator.h"
#include "otbVisualizationPixelTraits.h"
#include "otbObjectList.h"

#include "itkImageRegionConstIterator.h"
#include "otbUnaryFunctorImageFilter.h"
#include "itkBinaryFunctorImageFilter.h"

#include "itkCastImageFilter.h"

namespace otb
{

namespace Functor
{
// Functor to compare RGB values
template <class TInput>
class VectorLexicographicCompare
{
public:
  bool operator()(const TInput& l, const TInput& r) const
  {
    unsigned int size = ( l.Size() < r.Size() ? l.Size() : r.Size());
    for (unsigned int i=0; i < size; ++i)
    {
      if (l[i] < r[i])
      {
        return true;
      }
      else if (l[i] > r[i])
      {
        return false;
      }
    }
    return false;
  }
};

// Functor to map vectors
template<class TInput, class TOutput>
class VectorMapping
{
public:
  typedef typename TOutput::ValueType ValueType;

  VectorMapping() {}
  virtual ~VectorMapping() {}

  typedef std::map<TInput, TOutput, VectorLexicographicCompare<TInput> > ChangeMapType;

  void SetOutputSize(unsigned int nb)
  {
    m_OutputSize = nb;
  }
  unsigned int GetOutputSize()
  {
    return m_OutputSize;
  }
  bool operator !=(const VectorMapping& other) const
  {
    if (m_ChangeMap != other.m_ChangeMap)
      {
      return true;
      }
    return false;
  }
  bool operator ==(const VectorMapping& other) const
  {
    return !(*this != other);
  }
  TOutput GetChange(const TInput& original)
  {
    return m_ChangeMap[original];
  }

  void SetChange(const TInput& original, const TOutput& result)
  {
    m_ChangeMap[original] = result;
  }

  void SetChangeMap(const ChangeMapType& changeMap)
  {
    m_ChangeMap = changeMap;
  }

  void ClearChangeMap()
  {
    m_ChangeMap.clear();
  }

  void SetNotFoundValue(const TOutput& notFoundValue)
  {
    m_NotFoundValue = notFoundValue;
  }

  TOutput GetNotFoundValue()
  {
    return m_NotFoundValue;
  }

  inline TOutput operator ()(const TInput& A)
  {
    TOutput out;
    out.SetSize(m_OutputSize);

    if (m_ChangeMap.find(A) != m_ChangeMap.end())
      {
      out = m_ChangeMap[A];
      }
    else
      {
      out = m_NotFoundValue;
      }
    return out;
  }

private:
  ChangeMapType m_ChangeMap;
  unsigned int  m_OutputSize;   // number of components in output image
  TOutput       m_NotFoundValue;
};

// Functor to compute mean pixel value for each label
template <class TLabel, class TValue> class RGBFromImageValueFunctor
{
public:
  typedef std::map<TLabel, TValue> MeanValueMapType;

  void SetMaxVal(const TValue maxVal)
  {
    m_MaxVal.SetSize(maxVal.Size());
    for (unsigned int index = 0; index < maxVal.Size(); index++)
      {
      m_MaxVal.SetElement(index, maxVal.GetElement(index));
      }
  }

  void SetMinVal(const TValue minVal)
  {
    m_MinVal.SetSize(minVal.Size());
    for (unsigned int index = 0; index < minVal.Size(); index++)
      {
      m_MinVal.SetElement(index, minVal.GetElement(index));
      }
  }

  TValue GetMaxVal()
  {
    return m_MaxVal;
  }

  TValue GetMinVal()
  {
    return m_MinVal;
  }

  void AddNewLabel(TLabel label, TValue value)
  {

    TValue newValue;
    newValue.SetSize(value.Size());
    for (unsigned int index = 0; index < value.Size(); index++)
      {
      if (value[index] < m_MinVal[index])
        newValue[index] = m_MinVal[index];
      else
        if (value[index] > m_MaxVal[index])
          newValue[index] = m_MaxVal[index];
        else newValue[index] = value[index];
      }
    m_LabelToImageIntensityMap[label] = newValue;
    m_WeigthingMap[label] = 1;
  }

  void AddValue(const TLabel& label, const TValue& value)
  {
    TValue currentValue = m_LabelToImageIntensityMap[label];
    m_WeigthingMap[label] = m_WeigthingMap[label] + 1;
    for (unsigned int index = 0; index < value.Size(); index++)
      {
      if (value[index] < m_MinVal[index])
        currentValue[index] += m_MinVal[index];
      else
        if (value[index] > m_MaxVal[index])
          currentValue[index] += m_MaxVal[index];
        else currentValue[index] += value[index];

        }

    m_LabelToImageIntensityMap[label] = currentValue;
  }

  /** operator */
  TLabel operator ()(const TLabel& label, const TValue& value)
  {
    if (m_LabelToImageIntensityMap.count(label)<=0)
      {
      AddNewLabel(label, value);
        }
      else
        {
        AddValue(label, value);
        }

    return label;
  }

  MeanValueMapType GetMeanIntensity()
  {
    MeanValueMapType MeanMap;

    typename std::map<TLabel, unsigned int>::iterator mapIt = m_WeigthingMap.begin();
    typename std::map<TLabel, TValue>::iterator sumIt = m_LabelToImageIntensityMap.begin();

    unsigned int pixelSize = m_MinVal.Size();
    while (mapIt != m_WeigthingMap.end())
      {
      TLabel i = mapIt->first;

      float weight = static_cast<float> (mapIt->second);
      TValue sum = sumIt->second;
      TValue value;
      value.SetSize(pixelSize);
      if (sum.Size() == 0)
        {
        value.Fill(0.0);
        }
      else
        {
        for (unsigned int index = 0; index < sum.Size(); index++)
          {
          value[index] = sum[index] / weight;
          }
        }

      MeanMap[i] = value;
      ++mapIt;
      ++sumIt;
      }
    return MeanMap;
  }

  std::map<TLabel, unsigned int> GetLabelArea()
  {
    return m_WeigthingMap;
  }

  /** Constructor */
  RGBFromImageValueFunctor()
  {
  }

  bool operator !=(const RGBFromImageValueFunctor& other) const
  {

    return ((&m_LabelToImageIntensityMap) != &(other.m_LabelToImageIntensityMap));
  }
private:

  std::map<TLabel, unsigned int> m_WeigthingMap; //counter
  MeanValueMapType m_LabelToImageIntensityMap;
  TValue m_MinVal;
  TValue m_MaxVal;

};

} //end namespace Functor

namespace Wrapper
{

class ColorMapping: public Application
{
public:
/** Standard class typedefs. */
  typedef ColorMapping      Self;
  typedef Application                   Superclass;
  typedef itk::SmartPointer<Self>       Pointer;
  typedef itk::SmartPointer<const Self> ConstPointer;

  /** Standard macro */
  itkNewMacro(Self);

  itkTypeMacro(ColorMapping, otb::Application);

  typedef FloatImageType::PixelType   PixelType;
  typedef UInt16ImageType             LabelImageType;
  typedef LabelImageType::PixelType   LabelType;
  typedef UInt8VectorImageType        VectorImageType;
  typedef VectorImageType::PixelType  VectorPixelType;
  typedef UInt8RGBImageType           RGBImageType;
  typedef RGBImageType::PixelType     RGBPixelType;

  typedef UInt16VectorImageType                       LabelVectorImageType;
  typedef LabelVectorImageType::PixelType             LabelVectorType;

  typedef itk::NumericTraits
    <FloatVectorImageType::PixelType>::ValueType      ScalarType;
  typedef itk::VariableLengthVector<ScalarType>       SampleType;
  typedef itk::Statistics::ListSample<SampleType>     ListSampleType;

  typedef itk::ImageRegionConstIterator
    <FloatVectorImageType>                            IteratorType;
  typedef itk::ImageRegionConstIterator
    <UInt16ImageType>                                 LabelIteratorType;

  // Manual label LUT
  typedef otb::ChangeLabelImageFilter
    <LabelImageType, VectorImageType>                 ChangeLabelFilterType;

  // Segmentation contrast maximisation LUT
  typedef itk::LabelToRGBImageFilter
    <LabelImageType, RGBImageType>                    LabelToRGBFilterType;

  // Continuous LUT mapping
  typedef itk::ScalarToRGBColormapImageFilter
    <FloatImageType, RGBImageType>                    ColorMapFilterType;
  typedef otb::Functor::ReliefColormapFunctor
    <PixelType, RGBPixelType>                         ReliefColorMapFunctorType;

  // Image support LUT
  typedef RAMDrivenStrippedStreamingManager
    <FloatVectorImageType>                            RAMDrivenStrippedStreamingManagerType;
  typedef otb::StreamingShrinkImageFilter
    <FloatVectorImageType, FloatVectorImageType>       ImageSamplingFilterType;
  typedef itk::Statistics::DenseFrequencyContainer    DFContainerType;
  typedef itk::NumericTraits<PixelType>::RealType     RealScalarType;
  typedef itk::VariableLengthVector<RealScalarType>   InternalPixelType;
  typedef otb::ListSampleToHistogramListGenerator
    <ListSampleType, ScalarType, DFContainerType>     HistogramFilterType;
  typedef itk::Statistics::Histogram
    <RealScalarType, 1, DFContainerType>              HistogramType;
  typedef ObjectList<HistogramType>                   HistogramListType;
  typedef HistogramType::Pointer                      HistogramPointerType;
  typedef otb::ImageMetadataInterfaceBase             ImageMetadataInterfaceType;
  typedef Functor::RGBFromImageValueFunctor
    <LabelType, FloatVectorImageType::PixelType>      RGBFromImageValueFunctorType;
  typedef itk::BinaryFunctorImageFilter
    <LabelImageType, FloatVectorImageType,
    LabelImageType, RGBFromImageValueFunctorType>     RGBFromImageValueFilterType;

  // Inverse mapper for color->label operation
  typedef otb::UnaryFunctorImageFilter
    <RGBImageType, LabelVectorImageType,
    Functor::VectorMapping
      <RGBPixelType, LabelVectorType> >               ColorToLabelFilterType;

  // Streaming the input image for color->label operation
  typedef otb::StreamingTraits<RGBImageType>          StreamingTraitsType;
  typedef itk::ImageRegionSplitter<2>                 SplitterType;
  typedef RGBImageType::RegionType                    RegionType;
  typedef itk::ImageRegionConstIterator<RGBImageType> RGBImageIteratorType;

  // Caster to convert a FloatImageType to LabelImageType
  typedef itk::CastImageFilter
    <FloatImageType, LabelImageType>                   CasterToLabelImageType;

private:
  void DoInit()
  {
    SetName("ColorMapping");
    SetDescription("Maps an input label image to 8-bits RGB using look-up tables.");

    SetDocName("Color Mapping");
    SetDocLongDescription("This application allows to map a label image to a 8-bits RGB image (in both ways) using different methods.\n"
                          " -The custom method allows to use a custom look-up table. The look-up table is loaded "
                          "from a text file where each line describes an entry. The typical use of this method is to colorise a "
                          "classification map.\n -The continuous method allows to map a range of values in a scalar input image "
                          "to a colored image using continuous look-up table, in order to enhance image interpretation. Several "
                          "look-up tables can been chosen with different color ranges.\n-The optimal method computes an optimal "
                          "look-up table. When processing a segmentation label image (label to color), the color difference between"
                          " adjacent segmented regions is maximized. When processing an unknown color image (color to label), all "
                          "the present colors are mapped to a continuous label list.\n - The support image method uses a color support "
                          "image to associate an average color to each region.");
    SetDocLimitations("The segmentation optimal method does not support streaming, and thus large images. The operation color to label "
                      "is not implemented for the methods continuous LUT and support image LUT.\n ColorMapping using support image is not threaded.");
    SetDocAuthors("OTB-Team");
    SetDocSeeAlso("ImageSVMClassifier");

    AddDocTag("Utilities");
    AddDocTag(Tags::Manip);
    AddDocTag(Tags::Meta);
    AddDocTag(Tags::Learning);

    // Build lut map

    m_LutMap["Red"]=ColorMapFilterType::Red;
    m_LutMap["Green"]=ColorMapFilterType::Green;
    m_LutMap["Blue"]=ColorMapFilterType::Blue;
    m_LutMap["Grey"]=ColorMapFilterType::Grey;
    m_LutMap["Hot"]=ColorMapFilterType::Hot;
    m_LutMap["Cool"]=ColorMapFilterType::Cool;
    m_LutMap["Spring"]=ColorMapFilterType::Spring;
    m_LutMap["Summer"]=ColorMapFilterType::Summer;
    m_LutMap["Autumn"]=ColorMapFilterType::Autumn;
    m_LutMap["Winter"]=ColorMapFilterType::Winter;
    m_LutMap["Copper"]=ColorMapFilterType::Copper;
    m_LutMap["Jet"]=ColorMapFilterType::Jet;
    m_LutMap["HSV"]=ColorMapFilterType::HSV;
    m_LutMap["OverUnder"]=ColorMapFilterType::OverUnder;

    AddParameter(ParameterType_InputImage, "in", "Input Image");
    SetParameterDescription("in", "Input image filename");
    AddParameter(ParameterType_OutputImage, "out", "Output Image");
    SetParameterDescription("out","Output image filename");

    AddRAMParameter();

    // --- OPERATION --- : Label to color / Color to label
    AddParameter(ParameterType_Choice, "op", "Operation");
    SetParameterDescription("op","Selection of the operation to execute (default is : label to color).");

    AddChoice("op.labeltocolor","Label to color");

    AddChoice("op.colortolabel","Color to label");
    AddParameter(ParameterType_Int, "op.colortolabel.notfound","Not Found Label");
    SetParameterDescription("op.colortolabel.notfound","Label to use for unknown colors.");
    SetDefaultParameterInt("op.colortolabel.notfound", 404);
    MandatoryOff("op.colortolabel.notfound");

    // --- MAPPING METHOD ---
    AddParameter(ParameterType_Choice, "method", "Color mapping method");
    SetParameterDescription("method","Selection of color mapping methods and their parameters.");

    // Custom LUT
    AddChoice("method.custom","Color mapping with custom labeled look-up table");
    SetParameterDescription("method.custom","Apply a user-defined look-up table to a labeled image. Look-up table is loaded from a text file.");
    AddParameter(ParameterType_InputFilename, "method.custom.lut", "Look-up table file");
    SetParameterDescription("method.custom.lut",  "An ASCII file containing the look-up table\n"
                            "with one color per line\n"
                            "(for instance the line '1 255 0 0' means that all pixels with label 1 will be replaced by RGB color 255 0 0)\n"
                            "Lines beginning with a # are ignored");

    // Continuous LUT
    AddChoice("method.continuous","Color mapping with continuous look-up table");
    SetParameterDescription("method.continuous","Apply a continuous look-up table to a range of input values.");
    AddParameter(ParameterType_Choice,"method.continuous.lut","Look-up tables");
    SetParameterDescription("method.continuous.lut","Available look-up tables.");

    AddChoice("method.continuous.lut.red","Red");
    AddChoice("method.continuous.lut.green","Green");
    AddChoice("method.continuous.lut.blue","Blue");
    AddChoice("method.continuous.lut.grey","Grey");
    AddChoice("method.continuous.lut.hot","Hot");
    AddChoice("method.continuous.lut.cool","Cool");
    AddChoice("method.continuous.lut.spring","Spring");
    AddChoice("method.continuous.lut.summer","Summer");
    AddChoice("method.continuous.lut.autumn","Autumn");
    AddChoice("method.continuous.lut.winter","Winter");
    AddChoice("method.continuous.lut.copper","Copper");
    AddChoice("method.continuous.lut.jet","Jet");
    AddChoice("method.continuous.lut.hsv","HSV");
    AddChoice("method.continuous.lut.overunder","OverUnder");
    AddChoice("method.continuous.lut.relief","Relief");

    AddParameter(ParameterType_Float,"method.continuous.min","Mapping range lower value");
    SetParameterDescription("method.continuous.min","Set the lower input value of the mapping range.");
    SetParameterFloat("method.continuous.min", 0.);

    AddParameter(ParameterType_Float,"method.continuous.max","Mapping range higher value");
    SetParameterDescription("method.continuous.max","Set the higher input value of the mapping range.");
    SetParameterFloat("method.continuous.max", 255.);

    // Optimal LUT
    AddChoice("method.optimal","Compute an optimized look-up table");
    SetParameterDescription("method.optimal","[label to color] Compute an optimal look-up table such that neighboring labels"
                            " in a segmentation are mapped to highly contrasted colors.\n"
                            "[color to label] Searching all the colors present in the image to compute a continuous label list");
    AddParameter(ParameterType_Int,"method.optimal.background", "Background label");
    SetParameterDescription("method.optimal.background","Value of the background label");
    SetParameterInt("method.optimal.background", 0);
    SetMinimumParameterIntValue("method.optimal.background", 0);
    SetMaximumParameterIntValue("method.optimal.background", 255);

    // Support image LUT
    AddChoice("method.image","Color mapping with look-up table calculated on support image");
    AddParameter(ParameterType_InputImage, "method.image.in", "Support Image");
    SetParameterDescription("method.image.in", "Support image filename. LUT is calculated using the mean af pixel value on the area."
                            " First of all image is normalized with extrema rejection");
    AddParameter(ParameterType_Int, "method.image.low", "lower quantile");
    SetParameterDescription("method.image.low","lower quantile for image normalization");
    MandatoryOff("method.image.low");
    SetParameterInt("method.image.low", 2);
    SetMinimumParameterIntValue("method.image.low", 0);
    SetMaximumParameterIntValue("method.image.low", 100);
    AddParameter(ParameterType_Int, "method.image.up", "upper quantile");
    SetParameterDescription("method.image.up","upper quantile for image normalization");
    MandatoryOff("method.image.up");
    SetParameterInt("method.image.up", 2);
    SetMinimumParameterIntValue("method.image.up", 0);
    SetMaximumParameterIntValue("method.image.up", 100);


    // Doc example parameter settings
    SetDocExampleParameterValue("in", "ROI_QB_MUL_1_SVN_CLASS_MULTI.png");
    SetDocExampleParameterValue("method", "custom");
    SetDocExampleParameterValue("method.custom.lut", "ROI_QB_MUL_1_SVN_CLASS_MULTI_PNG_ColorTable.txt");
    SetDocExampleParameterValue("out", "Colorized_ROI_QB_MUL_1_SVN_CLASS_MULTI.tif");
 }

  void DoUpdateParameters()
  {
    // Make sure the operation color->label is not called with methods continuous or image.
    // These methods are not implemented for this operation yet.
    if (GetParameterInt("op")==1)
      {
      if (GetParameterInt("method")==1 || GetParameterInt("method")==3)
        {
        otbAppLogWARNING("Override method : use optimal");
        SetParameterInt("method", 2);
        }
      }
  }

  void DoExecute()
  {
    if(GetParameterInt("op")==0)
    {
      ComputeLabelToColor();
    }
    else if(GetParameterInt("op")==1)
    {
      ComputeColorToLabel();
    }
  }

  void ComputeLabelToColor()
  {
    if (GetParameterInt("method") == 0)
      {
      m_CasterToLabelImage = CasterToLabelImageType::New();
      m_CasterToLabelImage->SetInput(GetParameterFloatImage("in"));
      m_CasterToLabelImage->InPlaceOn();

      m_CustomMapper = ChangeLabelFilterType::New();
      m_CustomMapper->SetInput(m_CasterToLabelImage->GetOutput());
      m_CustomMapper->SetNumberOfComponentsPerPixel(3);

      ReadLutFromFile(true);

      SetParameterOutputImage("out", m_CustomMapper->GetOutput());
      }
    else if (GetParameterInt("method") == 1)
      {
      m_ContinuousColorMapper = ColorMapFilterType::New();

      m_ContinuousColorMapper->SetInput(GetParameterFloatImage("in"));

      // Disable automatic scaling
      m_ContinuousColorMapper->UseInputImageExtremaForScalingOff();

      // Set the lut
      std::string lutTmp = GetParameterString("method.continuous.lut");
      std::string lutNameParam =  "method.continuous.lut." + lutTmp;
      std::string lut = GetParameterName(lutNameParam);

      otbAppLogINFO("LUT: "<<lut<<std::endl);

      if (lut == "Relief")
        {
        ReliefColorMapFunctorType::Pointer reliefFunctor = ReliefColorMapFunctorType::New();
        m_ContinuousColorMapper->SetColormap(reliefFunctor);
        }
      else
        {
        m_ContinuousColorMapper->SetColormap((ColorMapFilterType::ColormapEnumType) m_LutMap[lut]);
        }


      m_ContinuousColorMapper->GetColormap()->SetMinimumInputValue(GetParameterFloat("method.continuous.min"));
      m_ContinuousColorMapper->GetColormap()->SetMaximumInputValue(GetParameterFloat("method.continuous.max"));

      SetParameterOutputImage("out", m_ContinuousColorMapper->GetOutput());
      }
    else if (GetParameterInt("method") == 2)
      {
      m_CasterToLabelImage = CasterToLabelImageType::New();
      m_CasterToLabelImage->SetInput(GetParameterFloatImage("in"));
      m_CasterToLabelImage->InPlaceOn();

      m_SegmentationColorMapper = LabelToRGBFilterType::New();
      m_SegmentationColorMapper->SetInput(m_CasterToLabelImage->GetOutput());
      m_SegmentationColorMapper->SetBackgroundValue(GetParameterInt("method.optimal.background"));
      SetParameterOutputImage("out", m_SegmentationColorMapper->GetOutput());
      }
    else if (GetParameterInt("method") == 3)
      {
      otbAppLogINFO(" look-up table calculated on support image ");

      m_CasterToLabelImage = CasterToLabelImageType::New();
      m_CasterToLabelImage->SetInput(GetParameterFloatImage("in"));
      m_CasterToLabelImage->InPlaceOn();

      // image normalisation of the sampling //
      FloatVectorImageType::Pointer supportImage = this->GetParameterImage("method.image.in");
      supportImage->UpdateOutputInformation();

      //normalisation
      //first of all resampling

      //calculate split number
      RAMDrivenStrippedStreamingManagerType::Pointer
          streamingManager = RAMDrivenStrippedStreamingManagerType::New();
      int availableRAM = GetParameterInt("ram");
      streamingManager->SetAvailableRAMInMB(availableRAM);
      float bias = 2.0; // empiric value;
      streamingManager->SetBias(bias);
      FloatVectorImageType::RegionType largestRegion = supportImage->GetLargestPossibleRegion();
      FloatVectorImageType::SizeType largestRegionSize = largestRegion.GetSize();
      streamingManager->PrepareStreaming(supportImage, largestRegion);

      unsigned long nbDivisions = streamingManager->GetNumberOfSplits();
      unsigned long largestPixNb = largestRegionSize[0] * largestRegionSize[1];

      unsigned long maxPixNb = largestPixNb / nbDivisions;

      ImageSamplingFilterType::Pointer imageSampler = ImageSamplingFilterType::New();
      imageSampler->SetInput(supportImage);

      double theoricNBSamplesForKMeans = maxPixNb;

      const double upperThresholdNBSamplesForKMeans = 1000 * 1000;
      const double actualNBSamplesForKMeans = std::min(theoricNBSamplesForKMeans,
                                                        upperThresholdNBSamplesForKMeans);

      const double shrinkFactor = vcl_floor(
                                            vcl_sqrt(
                                                      supportImage->GetLargestPossibleRegion().GetNumberOfPixels()
                                                          / actualNBSamplesForKMeans));
      imageSampler->SetShrinkFactor(shrinkFactor);
      imageSampler->Update();

      otbAppLogINFO(<<imageSampler->GetOutput()->GetLargestPossibleRegion().GetNumberOfPixels()<<""
          " sample will be used to estimate extrema value for outliers rejection."<<std::endl);

      // use histogram to compute quantile value


      FloatVectorImageType::Pointer histogramSource;
      histogramSource = imageSampler->GetOutput();
      histogramSource->SetRequestedRegion(imageSampler->GetOutput()->GetLargestPossibleRegion());

      // Iterate on the image
      itk::ImageRegionConstIterator<FloatVectorImageType> it(histogramSource,
                                                              histogramSource->GetBufferedRegion());

      // declare a list to store the samples
      ListSampleType::Pointer listSample = ListSampleType::New();
      listSample->Clear();

      unsigned int sampleSize = VisualizationPixelTraits::PixelSize(it.Get());
      listSample->SetMeasurementVectorSize(sampleSize);

      // Fill the samples list
      it.GoToBegin();
      while (!it.IsAtEnd())
        {
        SampleType sample(sampleSize);
        VisualizationPixelTraits::Convert(it.Get(), sample);
        listSample->PushBack(sample);
        ++it;
        }

      // assign listSample

      HistogramFilterType::Pointer histogramFilter = HistogramFilterType::New();
      //histogramFilter->SetListSample(pixelRepresentationListSample);
      histogramFilter->SetListSample(listSample);

      histogramFilter->SetNumberOfBins(256);
      histogramFilter->NoDataFlagOn();

      // Generate
      histogramFilter->Update();
      HistogramListType::Pointer histogramList = histogramFilter->GetOutput(); //
      // HistogramPointerType histoBand=histogramList->GetNelements(0);
      //  std::cout<<histoBand->GetFrequency(0, 0)<<std::endl;


      ImageMetadataInterfaceType::Pointer
          metadataInterface = ImageMetadataInterfaceFactory::CreateIMI(supportImage->GetMetaDataDictionary());

      std::vector<unsigned int> RGBIndex;

      if (supportImage->GetNumberOfComponentsPerPixel() < 3)
        {
        RGBIndex.push_back(0);
        RGBIndex.push_back(0);
        RGBIndex.push_back(0);
        }
      else RGBIndex = metadataInterface->GetDefaultDisplay();
      otbAppLogINFO(" RGB index are "<<RGBIndex[0]<<" "<<RGBIndex[1]<<" "<<RGBIndex[2]<<std::endl);

      FloatVectorImageType::PixelType minVal;
      FloatVectorImageType::PixelType maxVal;
      minVal.SetSize(supportImage->GetNumberOfComponentsPerPixel());
      maxVal.SetSize(supportImage->GetNumberOfComponentsPerPixel());

      for (unsigned int index = 0; index < supportImage->GetNumberOfComponentsPerPixel(); index++)
        {
        minVal.SetElement(index, static_cast<FloatVectorImageType::PixelType::ValueType> (histogramList->GetNthElement(index)->Quantile(0, static_cast<float> (this->GetParameterInt("method.image.low"))/ 100.0)));
        maxVal.SetElement(index, static_cast<FloatVectorImageType::PixelType::ValueType> (histogramList->GetNthElement(index)->Quantile(0, (100.0- static_cast<float> (this->GetParameterInt("method.image.up")))/ 100.0)));
        }

      // create functor
      RGBFromImageValueFunctorType functor;
      functor.SetMinVal(minVal);
      functor.SetMaxVal(maxVal);

      m_RGBFromImageValueFilter = RGBFromImageValueFilterType::New();
      m_RGBFromImageValueFilter->SetInput1(m_CasterToLabelImage->GetOutput());
      m_RGBFromImageValueFilter->SetInput2(this->GetParameterImage("method.image.in"));
      m_RGBFromImageValueFilter->SetFunctor(functor);
      m_RGBFromImageValueFilter->SetNumberOfThreads(1);

      m_RGBFromImageValueFilter->Update();

      std::map<LabelType, FloatVectorImageType::PixelType>
          labelToMeanIntensityMap = m_RGBFromImageValueFilter->GetFunctor().GetMeanIntensity();

      m_RBGFromImageMapper = ChangeLabelFilterType::New();
      m_RBGFromImageMapper->SetInput(m_CasterToLabelImage->GetOutput());
      m_RBGFromImageMapper->SetNumberOfComponentsPerPixel(3);

      std::map<LabelType, FloatVectorImageType::PixelType>::const_iterator
          mapIt = labelToMeanIntensityMap.begin();
      FloatVectorImageType::PixelType meanValue;

      otbAppLogINFO("The map contains :"<<labelToMeanIntensityMap.size()<<" labels."<<std::endl);
      VectorPixelType color(3);
      while (mapIt != labelToMeanIntensityMap.end())
        {

        LabelType clabel = mapIt->first;
        meanValue = mapIt->second; //meanValue.Size() is null if label is not present in label image
        if (clabel == 0 || meanValue.Size()==0)
          {
          color.Fill(0.0);
          }
        else
          {
          for (int RGB = 0; RGB < 3; RGB++)
            {
            unsigned int dispIndex = RGBIndex[RGB];

            color[RGB] = ((meanValue.GetElement(dispIndex) - minVal.GetElement(dispIndex)) / (
                maxVal.GetElement(dispIndex) - minVal.GetElement(dispIndex))) * 255.0;
            }
          }
        otbAppLogINFO("Adding color mapping " << clabel << " -> [" << (int) color[0] << " " << (int) color[1] << " "<< (int) color[2] << " ]" << std::endl);
        m_RBGFromImageMapper->SetChange(clabel, color);

        ++mapIt;
        }

      SetParameterOutputImage("out", m_RBGFromImageMapper->GetOutput());
      }
  }

  void ComputeColorToLabel()
  {
    if (GetParameterInt("method")==1 || GetParameterInt("method")==3)
      {
      otbAppLogWARNING("Case not implemented");
      return;
      }

    RGBImageType::Pointer input = GetParameterUInt8RGBImage("in");
    m_InverseMapper = ColorToLabelFilterType::New();
    m_InverseMapper->SetInput(input);
    m_InverseMapper->GetFunctor().SetOutputSize(1);
    LabelVectorType notFoundValue(1);
    notFoundValue[0] = GetParameterInt("op.colortolabel.notfound");
    m_InverseMapper->GetFunctor().SetNotFoundValue(notFoundValue);

    if(GetParameterInt("method")==0)
      {
      ReadLutFromFile(false);

      SetParameterOutputImage<LabelVectorImageType>("out", m_InverseMapper->GetOutput());
      }
    else if(GetParameterInt("method")==2)
      {
      // Safe mode : the LUT is computed with the colors found in the image
      std::set<RGBPixelType, Functor::VectorLexicographicCompare<RGBPixelType> > colorList;
      RGBPixelType background;
      background.Fill(0); //we assume the background will be black
      LabelType currentLabel;
      currentLabel = GetParameterInt("method.optimal.background");
      colorList.insert(background);
      LabelVectorType currentVectorLabel(1);
      currentVectorLabel[0] = currentLabel;
      m_InverseMapper->GetFunctor().SetChange(background, currentVectorLabel);
      ++currentLabel;

      // Setting up local streaming capabilities
      RegionType largestRegion = input->GetLargestPossibleRegion();
      SplitterType::Pointer splitter = SplitterType::New();
      unsigned int numberOfStreamDivisions;
      numberOfStreamDivisions = StreamingTraitsType::CalculateNumberOfStreamDivisions(input,
                                          largestRegion,
                                          splitter,
                                          otb::SET_BUFFER_MEMORY_SIZE,
                                          0, 1048576*GetParameterInt("ram"), 0);

      otbAppLogINFO("Number of divisions : "<<numberOfStreamDivisions);

      // iteration over stream divisions
      RegionType streamingRegion;
      for (unsigned int index = 0; index<numberOfStreamDivisions; index++)
        {
        streamingRegion = splitter->GetSplit(index, numberOfStreamDivisions, largestRegion);
        input->SetRequestedRegion(streamingRegion);
        input->PropagateRequestedRegion();
        input->UpdateOutputData();

        RGBImageIteratorType it(input, streamingRegion);
        it.GoToBegin();
        while ( !it.IsAtEnd())
          {
          // if the color isn't registered, it is added to the color map
          if (colorList.find(it.Get())==colorList.end())
            {
            colorList.insert(it.Get());
            currentVectorLabel[0] = currentLabel;
            m_InverseMapper->GetFunctor().SetChange(it.Get(), currentVectorLabel);
            ++currentLabel;
            }
          ++it;
          }
        }

      SetParameterOutputImage<LabelVectorImageType>("out", m_InverseMapper->GetOutput());
      }
  }

  void ReadLutFromFile(bool putLabelBeforeColor)
  {
    std::ifstream ifs;

    ifs.open(GetParameterString("method.custom.lut").c_str());

    if (!ifs)
      {
      itkExceptionMacro("Can not read file " << GetParameterString("method.custom.lut") << std::endl);
      }

    otbAppLogINFO("Parsing color map file " << GetParameterString("method.custom.lut") << "." << std::endl);

    RGBPixelType    rgbcolor;
    LabelVectorType cvlabel(1);

    while (!ifs.eof())
      {
      std::string line;
      std::getline(ifs, line);

      // Avoid commented lines or too short ones
      if (!line.empty() && line[0] != '#')
        {
        // retrieve the label
        std::string::size_type pos = line.find_first_of(" ", 0);
        LabelType clabel = atoi(line.substr(0, pos).c_str());
        ++pos;
        // Retrieve the color
        VectorPixelType color(3);
        color.Fill(0);
        for (unsigned int i = 0; i < 3; ++i)
          {
          std::string::size_type nextpos = line.find_first_of(" ", pos);
          int value = atoi(line.substr(pos, nextpos).c_str());
          if (value < 0 || value > 255)
            otbAppLogWARNING("WARNING: color value outside 8-bits range (<0 or >255). Value will be clamped." << std::endl);
          color[i] = static_cast<PixelType> (value);
          pos = nextpos + 1;
          nextpos = line.find_first_of(" ", pos);
          }
        otbAppLogINFO("Adding color mapping " << clabel << " -> [" << (int) color[0] << " " << (int) color[1] << " "<< (int) color[2] << " ]" << std::endl);
        if(putLabelBeforeColor)
          {
          m_CustomMapper->SetChange(clabel, color);
          }
        else
          {
          cvlabel[0] = clabel;
          rgbcolor[0] = static_cast<int>(color[0]);
          rgbcolor[1] = static_cast<int>(color[1]);
          rgbcolor[2] = static_cast<int>(color[2]);
          m_InverseMapper->GetFunctor().SetChange(rgbcolor, cvlabel);
          }
        }
      }
    ifs.close();
  }


  ChangeLabelFilterType::Pointer m_CustomMapper;
  ColorMapFilterType::Pointer    m_ContinuousColorMapper;
  LabelToRGBFilterType::Pointer  m_SegmentationColorMapper;
  std::map<std::string, unsigned int> m_LutMap;
  ChangeLabelFilterType::Pointer m_RBGFromImageMapper;
  RGBFromImageValueFilterType::Pointer    m_RGBFromImageValueFilter;

  ColorToLabelFilterType::Pointer m_InverseMapper;

  CasterToLabelImageType::Pointer m_CasterToLabelImage;

};
}
}

OTB_APPLICATION_EXPORT(otb::Wrapper::ColorMapping)


