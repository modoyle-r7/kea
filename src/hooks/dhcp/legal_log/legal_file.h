// Copyright (C) 2016 Internet Systems Consortium, Inc. ("ISC")
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef LEGAL_FILE_H
#define LEGAL_FILE_H

/// @file legal_file.h Defines the class, LegalFile, which implements a
/// an appending text file which rotates to a new file on a daily basis.

#include <exceptions/exceptions.h>

#include <boost/shared_ptr.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>

#include <fstream>
#include <string>

namespace isc {
namespace legal_log {

/// @brief Thrown if a LegalFile encounters an error.
class LegalFileError : public isc::Exception {
public:
    LegalFileError(const char* file, size_t line, const char* what) :
        isc::Exception(file, line, what)
    {}
};

/// @brief LegalFile implements an appending text file which rotates
/// to a new file on a daily basis.  The physical file name(s) are
/// deteremined as follows:
///
/// Forms the current file name from:
///
///    <path>/<base_name>.<date>.txt
///
/// where:
///     path - is the pathname supplied via the constructor. The path
///     must exist and be writable by the process
///
///     base_name - an arbitrary text label supplied via the constructor
///
///     date - is the system date, at the time the file is opened, in local
///     time.  The format of the value is CCYYMMDD (century,year,month,day)
///
/// Prior to each write, the system date is compared to the current file date
/// to determine if rotation is necessary (i.e. day boundary has been crossed
/// since the last write).  If so, the current file is closed, and the new
/// file is created.
///
///The file does not impose any
/// particular format constraints upon content.
class LegalFile {
public:
    /// @brief Constructor
    ///
    /// Create a LegalFile for the given file name without opening the file.
    /// @param path - directory in which file(s) will be created
    /// @param base_name - base file name to use when creating files
    ///
    /// @throw LegalFileError if given file name is empty.
    LegalFile(const std::string& path, const std::string& base_name);

    /// @brief Destructor.
    ////
    /// The destructor does call the close method.
    virtual ~LegalFile();

    /// @brief Opens the current file for writing.
    ///
    /// Forms the current file name from:
    ///
    ///    <path_>/<base_name_>.<CCYYMMDD>.txt
    ///
    /// where CCYYMMDD is the current date in local time.
    ///
    /// and opens the file for appending. If the file does not exist
    /// it is created.  If the file is already open, the method simply
    /// returns.
    ///
    /// @throw LegalFileError if the file cannot be opened.
    virtual void open();

    /// @brief Closes the underlying file.
    ///
    /// Method is exception safe.
    virtual void close();

    /// @brief Rotates the file if necessary
    ///
    /// The system date (no time component) is latter than the current file date
    /// (i.e. day boundary has been crossed), the the current physical file is
    /// closed and replaced with a newly created and open file.
    virtual void rotate();

    /// @brief Returns true if the file is open.
    ///
    /// @return True if the physical file is open, false otherwise.
    virtual bool isOpen() const;

    /// @brief Appends a string to the current file
    ///
    /// Invokes rotate() and then attempts to add the given string
    /// followed by EOL to the end of the file.
    ///
    /// @param text the string to append
    ///
    /// @throw LegalFileError if the write fails
    virtual void writeln(const std::string& text);

    /// @brief Returns the current local date
    /// This is exposed primarily to simplify testing.
    virtual boost::gregorian::date today();

    /// @brief Returns the current system time
    /// This is exposed primarily to simplify testing.
    virtual time_t now();

    /// @brief Returns the current date and time as string
    ///
    /// Returns the current local date and time as a string based on the
    /// given format.  Maximum length of the result is 128 bytes.
    ///
    /// @param format - desired format for the string. Permissable formatting is
    /// that supported by strftime.  The default is: "%Y-%m-%d %H:%M:%S %Z"
    ///
    /// @return std::string containg the formatted current date and time
    /// @throw LegalFileError if the result string is larger than 128 bytes.
    virtual std::string getNowString(const std::string& format="%Y-%m-%d %H:%M:%S %Z");

    /// @brief Returns the current file name
    std::string getFileName() {
        return(file_name_);
    }

    /// @brief Returns the date of the current file
    boost::gregorian::date getFileDay() {
        return(file_day_);
    }

private:
    /// @brief Directory in which the file(s) will be created
    std::string path_;

    /// @brief Base name of the file
    std::string base_name_;

    /// @brief Date of current file
    boost::gregorian::date file_day_;

    /// @brief Full name of the current file
    std::string file_name_;

    /// @brief Output file stream.
    std::ofstream file_;
};

/// @brief Defines a smart pointer to a LegalFile.
typedef boost::shared_ptr<LegalFile> LegalFilePtr;

} // namespace legal_file
} // namespace isc

#endif
