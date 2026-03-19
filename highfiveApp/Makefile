TOP=..

include $(TOP)/configure/CONFIG

INC += HighFive/include/highfive/H5Attribute.hpp
INC += HighFive/include/highfive/H5DataSet.hpp
INC += HighFive/include/highfive/H5DataSpace.hpp
INC += HighFive/include/highfive/H5DataType.hpp
INC += HighFive/include/highfive/H5Easy.hpp
INC += HighFive/include/highfive/H5Exception.hpp
INC += HighFive/include/highfive/H5FileDriver.hpp
INC += HighFive/include/highfive/H5File.hpp
INC += HighFive/include/highfive/H5Group.hpp
INC += HighFive/include/highfive/H5Object.hpp
INC += HighFive/include/highfive/H5PropertyList.hpp
INC += HighFive/include/highfive/H5Reference.hpp
INC += HighFive/include/highfive/H5Selection.hpp
INC += HighFive/include/highfive/H5Utility.hpp

INC += HighFive/include/highfive/bits/H5Annotate_traits.hpp
INC += HighFive/include/highfive/bits/H5Annotate_traits_misc.hpp
INC += HighFive/include/highfive/bits/H5Attribute_misc.hpp
INC += HighFive/include/highfive/bits/H5ConverterEigen_misc.hpp
INC += HighFive/include/highfive/bits/H5Converter_misc.hpp
INC += HighFive/include/highfive/bits/H5DataSet_misc.hpp
INC += HighFive/include/highfive/bits/H5Dataspace_misc.hpp
INC += HighFive/include/highfive/bits/H5DataType_misc.hpp
INC += HighFive/include/highfive/bits/H5_definitions.hpp
INC += HighFive/include/highfive/bits/H5Exception_misc.hpp
INC += HighFive/include/highfive/bits/H5FileDriver_misc.hpp
INC += HighFive/include/highfive/bits/H5File_misc.hpp
INC += HighFive/include/highfive/bits/H5Iterables_misc.hpp
INC += HighFive/include/highfive/bits/H5Node_traits.hpp
INC += HighFive/include/highfive/bits/H5Node_traits_misc.hpp
INC += HighFive/include/highfive/bits/H5Object_misc.hpp
INC += HighFive/include/highfive/bits/H5Path_traits.hpp
INC += HighFive/include/highfive/bits/H5Path_traits_misc.hpp
INC += HighFive/include/highfive/bits/H5PropertyList_misc.hpp
INC += HighFive/include/highfive/bits/H5ReadWrite_misc.hpp
INC += HighFive/include/highfive/bits/H5Reference_misc.hpp
INC += HighFive/include/highfive/bits/H5Selection_misc.hpp
INC += HighFive/include/highfive/bits/H5Slice_traits.hpp
INC += HighFive/include/highfive/bits/H5Slice_traits_misc.hpp
INC += HighFive/include/highfive/bits/H5Utils.hpp

INC += HighFive/include/highfive/h5easy_bits/H5Easy_Eigen.hpp
INC += HighFive/include/highfive/h5easy_bits/H5Easy_misc.hpp
INC += HighFive/include/highfive/h5easy_bits/H5Easy_opencv.hpp
INC += HighFive/include/highfive/h5easy_bits/H5Easy_public.hpp
INC += HighFive/include/highfive/h5easy_bits/H5Easy_scalar.hpp
INC += HighFive/include/highfive/h5easy_bits/H5Easy_vector.hpp
INC += HighFive/include/highfive/h5easy_bits/H5Easy_xtensor.hpp

include $(TOP)/configure/RULES

build: apply_patches

apply_patches:
	@echo "Patching files"
# Patch H5Converter_misc.hpp for older GCC (< 5)
# TODO: I strongly advise that this is removed when using GCC >= 5
	-patch --batch -d $(TOP)/highfiveApp/HighFive -p1 -N < $(TOP)/highfiveApp/is_trivially_copyable.patch
