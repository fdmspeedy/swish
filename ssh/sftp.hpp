/**
    @file

    SSH SFTP subsystem.

    @if license

    Copyright (C) 2010, 2012, 2013  Alexander Lamaison <awl03@doc.ic.ac.uk>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

    In addition, as a special exception, the the copyright holders give you
    permission to combine this program with free software programs or the 
    OpenSSL project's "OpenSSL" library (or with modified versions of it, 
    with unchanged license). You may copy and distribute such a system 
    following the terms of the GNU GPL for this program and the licenses 
    of the other code concerned. The GNU General Public License gives 
    permission to release a modified version without this exception; this 
    exception also makes it possible to release a modified version which 
    carries forward this exception.

    @endif
*/

#ifndef SSH_SFTP_HPP
#define SSH_SFTP_HPP

#include <ssh/detail/libssh2/sftp.hpp>
#include <ssh/session.hpp>

#include <boost/cstdint.hpp> // uint64_t, uintmax_t
#include <boost/exception/info.hpp> // errinfo_api_function
#include <boost/filesystem/path.hpp> // path
#include <boost/iterator/iterator_facade.hpp> // iterator_facade
#include <boost/optional/optional.hpp>
#include <boost/shared_ptr.hpp> // shared_ptr
#include <boost/system/error_code.hpp> // errc
#include <boost/system/system_error.hpp>
#include <boost/throw_exception.hpp> // BOOST_THROW_EXCEPTION

#include <algorithm> // min
#include <cassert> // assert
#include <exception> // bad_alloc
#include <stdexcept> // invalid_argument
#include <string>
#include <vector>

#include <libssh2_sftp.h>

namespace ssh {
namespace sftp {

class file_attributes
{
public:

    enum file_type
    {
        normal_file,
        symbolic_link,
        directory,
        character_device,
        block_device,
        named_pipe,
        socket,
        unknown
    };

    file_type type() const
    {
        if (is_valid_attribute(LIBSSH2_SFTP_ATTR_PERMISSIONS))
        {
            switch (m_attributes.permissions & LIBSSH2_SFTP_S_IFMT)
            {
            case LIBSSH2_SFTP_S_IFIFO:
                return named_pipe;
            case LIBSSH2_SFTP_S_IFCHR:
                return character_device;
            case LIBSSH2_SFTP_S_IFDIR:
                return directory;
            case LIBSSH2_SFTP_S_IFBLK:
                return block_device;
            case LIBSSH2_SFTP_S_IFREG:
                return normal_file;
            case LIBSSH2_SFTP_S_IFLNK:
                return symbolic_link;
            case LIBSSH2_SFTP_S_IFSOCK:
                return socket;
            default:
                return unknown;
            }
        }
        else
        {
            return unknown;
        }
    }

    boost::optional<unsigned long> permissions() const
    {
        if (is_valid_attribute(LIBSSH2_SFTP_ATTR_PERMISSIONS))
        {
            return m_attributes.permissions;
        }
        else
        {
            return boost::optional<unsigned long>();
        }
    }

    boost::optional<boost::uint64_t> size() const
    {
        if (is_valid_attribute(LIBSSH2_SFTP_ATTR_SIZE))
        {
            return m_attributes.filesize;
        }
        else
        {
            return boost::optional<boost::uint64_t>();
        }
    }

    boost::optional<unsigned long> uid() const
    {
        if (is_valid_attribute(LIBSSH2_SFTP_ATTR_UIDGID))
        {
            return m_attributes.uid;
        }
        else
        {
            return boost::optional<unsigned long>();
        }
    }

    boost::optional<unsigned long> gid() const
    {
        if (is_valid_attribute(LIBSSH2_SFTP_ATTR_UIDGID))
        {
            return m_attributes.gid;
        }
        else
        {
            return boost::optional<unsigned long>();
        }
    }

    /**
     * @todo  Use Boost.DateTime or other decent datatype.
     */
    boost::optional<unsigned long> last_accessed() const
    {
        if (is_valid_attribute(LIBSSH2_SFTP_ATTR_ACMODTIME))
        {
            return m_attributes.atime;
        }
        else
        {
            return boost::optional<unsigned long>();
        }
    }

    /**
     * @todo  Use Boost.DateTime or other decent datatype.
     */
    boost::optional<unsigned long> last_modified() const
    {
        if (is_valid_attribute(LIBSSH2_SFTP_ATTR_ACMODTIME))
        {
            return m_attributes.mtime;
        }
        else
        {
            return boost::optional<unsigned long>();
        }
    }

private:
    friend class sftp_file;
    friend class sftp_channel; // to construct in attributes method

    explicit file_attributes(const LIBSSH2_SFTP_ATTRIBUTES& raw_attributes) :
       m_attributes(raw_attributes) {}

    bool is_valid_attribute(unsigned long attribute_type) const
    {
        return (m_attributes.flags & attribute_type) != 0;
    }

    LIBSSH2_SFTP_ATTRIBUTES m_attributes;
};

class sftp_file
{
public:
    sftp_file(
        const boost::filesystem::path& file, const std::string& long_entry,
        const LIBSSH2_SFTP_ATTRIBUTES& attributes)
        :
        m_file(file), m_long_entry(long_entry), m_attributes(attributes) {}

    std::string name() const { return m_file.filename(); }
    const boost::filesystem::path& path() const { return m_file; }
    const std::string& long_entry() const { return m_long_entry; }

    /**
     * @todo Get rid of this ASAP!
     */
    const LIBSSH2_SFTP_ATTRIBUTES& raw_attributes() const
    {
        return m_attributes.m_attributes;
    }

    const file_attributes& attributes() const
    {
        return m_attributes;
    }

private:
    boost::filesystem::path m_file;
    std::string m_long_entry;
    file_attributes m_attributes;
};

class sftp_channel;

namespace detail {

    inline boost::shared_ptr<LIBSSH2_SFTP_HANDLE> open_directory(
        boost::shared_ptr<LIBSSH2_SESSION> session,
        boost::shared_ptr<LIBSSH2_SFTP> channel,
        const boost::filesystem::path& path)
    {
        std::string path_string = path.string();

        return boost::shared_ptr<LIBSSH2_SFTP_HANDLE>(
            ::ssh::detail::libssh2::sftp::open(
                session.get(), channel.get(), path_string.data(),
                path_string.size(), 0, 0, LIBSSH2_SFTP_OPENDIR),
            ::libssh2_sftp_close_handle);
    }

}

/**
 * List the files and directories in a directory.
 *
 * The iterator is copyable but all copies are linked so that incrementing
 * one will increment all the copies.
 */
class directory_iterator : public boost::iterator_facade<
    directory_iterator, const sftp_file, boost::forward_traversal_tag,
    sftp_file>
{
public:

    /**
     * End-of-directory marker.
     */
    // ForwardIterators are REQUIRED to be default-constructible, yukky as
    // that is
    directory_iterator() {}

    // directory_iterator is not implemented in terms of the sftp_channel
    // public interface.  It uses the the channel's internals, so the channel
    // should control it.  Therefore the only way to create it is
    // via the channel class, which has special access to the private
    // constructor via the client-attorney idiom.

    /// @cond INTERNAL
    /**
     * Defines the single permitted factory of `directory_iterator`
     * instances.
     * This class calls the private constructor on behalf of the factory.
     * See http://stackoverflow.com/q/3217390/67013.
     */
    class factory_attorney
    {
    private:
        friend class sftp_channel;

        directory_iterator operator()(
            boost::shared_ptr<LIBSSH2_SESSION> session,
            boost::shared_ptr<LIBSSH2_SFTP> channel,
            const boost::filesystem::path& path)
        {
            return directory_iterator(session, channel, path);
        }

        directory_iterator operator()()
        {
            return directory_iterator();
        }
    };
    /// @endcond

private:

    directory_iterator(
        boost::shared_ptr<LIBSSH2_SESSION> session,
        boost::shared_ptr<LIBSSH2_SFTP> sftp_channel,
        const boost::filesystem::path& path)
        :
        m_session(session),
        m_channel(sftp_channel),
        m_directory(path),
        m_handle(detail::open_directory(m_session, m_channel, path)),
        m_attributes(LIBSSH2_SFTP_ATTRIBUTES())
    {
        next_file();
    }

    friend class boost::iterator_core_access;

    void increment()
    {
        if (m_handle == NULL)
            BOOST_THROW_EXCEPTION(std::range_error("No more files"));
        next_file();
    }

    bool equal(directory_iterator const& other) const
    {
        return this->m_handle == other.m_handle;
    }

    void next_file()
    {    
        // yuk! hardcoded buffer sizes. unfortunately, libssh2 doesn't
        // give us a choice so we allocate massive buffers here and then
        // take measures later to reduce the footprint

        std::vector<char> filename_buffer(1024, '\0');
        std::vector<char> longentry_buffer(1024, '\0');
        LIBSSH2_SFTP_ATTRIBUTES attrs = LIBSSH2_SFTP_ATTRIBUTES();

        int rc = ::ssh::detail::libssh2::sftp::readdir_ex(
            m_session.get(), m_channel.get(), m_handle.get(),
            &filename_buffer[0], filename_buffer.size(), &longentry_buffer[0],
            longentry_buffer.size(), &attrs);

        if (rc == 0) // end of files
        {
            m_handle.reset();
        }
        else
        {
            assert(rc > 0);

            // copy attributes to member one we know we're overwriting the
            // last-retrieved file's properties
            m_attributes = attrs;

            // we don't assume that the filename is null-terminated but rc
            // holds the number of bytes written to the buffer so we can shrink
            // the filename string to that size
            m_file_name = std::string(
                &filename_buffer[0],
                (std::min)(static_cast<size_t>(rc), filename_buffer.size()));

            // the long entry must be usable in an ls -l listing according to
            // the standard so I'm interpreting this to mean it can't contain
            // embedded NULLs so we force NULL-termination and then allocate
            // the string to be the NULL-terminated size which will likely be
            // much smaller than the buffer
            longentry_buffer[longentry_buffer.size() - 1] = '\0';
            m_long_entry = std::string(&longentry_buffer[0]);
        }
    }

    sftp_file dereference() const
    {
        if (m_handle == NULL)
            BOOST_THROW_EXCEPTION(
                std::logic_error("Can't dereference the end of a collection"));

        return sftp_file(m_directory / m_file_name, m_long_entry, m_attributes);
    }

    boost::shared_ptr<LIBSSH2_SESSION> m_session;
    boost::shared_ptr<LIBSSH2_SFTP> m_channel;
    boost::filesystem::path m_directory;
    boost::shared_ptr<LIBSSH2_SFTP_HANDLE> m_handle;

    /// @name Properties of last successfully listed file.
    // @{
    std::string m_file_name;
    std::string m_long_entry;
    LIBSSH2_SFTP_ATTRIBUTES m_attributes;
    // @}
};

namespace detail {

    BOOST_SCOPED_ENUM_START(path_status)
    {
        non_existent,
            non_directory,
            directory
    };
    BOOST_SCOPED_ENUM_END;


    inline BOOST_SCOPED_ENUM(path_status) check_status(
        sftp_channel channel, const boost::filesystem::path& path);

}

BOOST_SCOPED_ENUM_START(overwrite_behaviour)
{
    /**
     * Do not overwrite an existing file at the destination.
     * 
     * If the file exists function will throw an exception.
     */
    prevent_overwrite,

    /**
     * Overwrite any existing file at the destination.
     *
     * The SFTP server may not support overwriting files, in which case this
     * acts like `prevent_overwrite`.
     */
    allow_overwrite,

    /**
     * Overwrite any existing file using *only* atomic methods.  If atomic methods
     * are not available on the server, the overwrite will not be performed by
     * other methods and the function will throw an exception.
     *
     * The SFTP server may not support overwriting files, in which case this
     * acts like `prevent_overwrite`.
     */
    atomic_overwrite
};
BOOST_SCOPED_ENUM_END

class sftp_input_device;
class sftp_output_device;
class sftp_io_device;

class sftp_channel
{
public:

    /**
     * Open a new SFTP channel in an SSH session.
     */
    explicit sftp_channel(::ssh::session session)
        :
    m_session(::ssh::session::access_attorney::get_pointer(session)),
    m_sftp(
        boost::shared_ptr<LIBSSH2_SFTP>(
            ::ssh::detail::libssh2::sftp::init(m_session.get()),
            ::libssh2_sftp_shutdown)) {}

    /**
     * Create an iterator over the contents of the given directory.
     */
    directory_iterator directory_iterator(const boost::filesystem::path& path)
    {
        return ssh::sftp::directory_iterator::factory_attorney()(
            m_session, m_sftp, path);
    }

    /**
     * Create an iterator marking the end of a directory.
     */
    ssh::sftp::directory_iterator directory_iterator()
    {
        return ssh::sftp::directory_iterator::factory_attorney()();
    }
    
    /**
     * Query a file for its attributes.
     *
     * If @a follow_links is @c true, the file that is queried is the target of
     * any chain of links.  Otherwise, it is the link itself.
     *
     * @todo Split into `status` and `symlink_status` to mirror Boost.Filesystem
     *       API.
     */
    file_attributes attributes(
        const boost::filesystem::path& file, bool follow_links)
    {
        std::string file_path = file.string();
        LIBSSH2_SFTP_ATTRIBUTES attributes = LIBSSH2_SFTP_ATTRIBUTES();
        ::ssh::detail::libssh2::sftp::stat(
            m_session.get(), m_sftp.get(), file_path.data(),
            file_path.size(),
            (follow_links) ? LIBSSH2_SFTP_STAT : LIBSSH2_SFTP_LSTAT,
            &attributes);

        return file_attributes(attributes);
    }

    boost::filesystem::path resolve_link_target(
        const boost::filesystem::path& link)
    {
        std::string link_string = link.string();

        return symlink_resolve(
            link_string.data(), link_string.size(), LIBSSH2_SFTP_READLINK);
    }

    boost::filesystem::path canonical_path(const boost::filesystem::path& link)
    {
        std::string link_string = link.string();

        return symlink_resolve(
            link_string.data(), link_string.size(), LIBSSH2_SFTP_REALPATH);
    }

    /**
     * Create a symbolic link.
     *
     * @param link     Path to the new link on the remote filesystem. Must not
     *                 already exist.
     * @param target   Path of file or directory to be linked to.
     *
     * @WARNING  All versions of OpenSSH and probably many other servers are
     *           implemented incorrectly and swap the order of the @p link and
     *           @p target parameters.  To connect to these servers you will
     *           have to pass the parameters to this function in the wrong
     *           order!
     */
    void create_symlink(
        const boost::filesystem::path& link,
        const boost::filesystem::path& target)
    {
        std::string link_string = link.string();
        std::string target_string = target.string();

        ::ssh::detail::libssh2::sftp::symlink(
            m_session.get(), m_sftp.get(), link_string.data(),
            link_string.size(), target_string.data(), target_string.size());
    }

    /**
     * Change one path to a file with another.
     *
     * After this function completes, `source` is no longer a path to the
     * file that it referenced before calling the function, and `destination`
     * is a new path to that file.
     *
     * @param channel
     *     SFTP connection.
     * @param source
     *     Path to the file on the remote filesystem. File must already exist.
     * @param destination
     *     Path to which the file will be moved.  File may already exist.  If it 
     *     does exist and `allow_overwrite` is `false`, the function will throw
     *     an exception.
     * @param overwrite_hint
     *     Optional hint suggesting preferred overwrite behaviour if 
     *     `destination`
     *     is already a path to a file before this function is called.  Only
     *     `prevent_overwrite` is guaranteed to be obeyed.  All other flags are
     *     suggestions that the server is free to disregard (most SFTP servers 
     *     disregard these flags).  If it does so and `destination` is already a
     *     path to a file, this function will throw an unspecified 
     *     `boost::system::system_error`.
     *
     * @throws `boost::system::system_error` if `destination` is already a
     *         path to a file before this function is called and either
     *         `prevent_overwrite` is specified as `overwrite_hint` or the
     *         server did not support the given hint.
     *
     * `atomic_overwrite` is the default value of `overwrite_hint` to give the
     * closest alignment to POSIX/Boost.Filesystem `rename`.  However, as
     * explained above, the server is free to refuse to overwrite in the
     * presence of an existing `destination`.  Therefore the APIs do not align
     * completely.
     *
     * @todo Not currently supporting the NATIVE flag as it's not at all clear
     *       what it does.
     */
    void rename(
        const boost::filesystem::path& source,
        const boost::filesystem::path& destination,
        BOOST_SCOPED_ENUM(overwrite_behaviour) overwrite_hint
            =overwrite_behaviour::atomic_overwrite)
    {
        std::string source_string = source.string();
        std::string destination_string = destination.string();

        int flags;
        switch (overwrite_hint)
        {
        case overwrite_behaviour::prevent_overwrite:
            flags = 0;
            break;

        case overwrite_behaviour::allow_overwrite:
            flags = LIBSSH2_SFTP_RENAME_OVERWRITE;
            break;

        case overwrite_behaviour::atomic_overwrite:
            // The spec says OVERWRITE is implied by ATOMIC but specifying both
            // to be on the safe side
            flags = LIBSSH2_SFTP_RENAME_OVERWRITE | LIBSSH2_SFTP_RENAME_ATOMIC;
            break;

        default:
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("Unrecognised overwrite behaviour"));
        }

        ::ssh::detail::libssh2::sftp::rename(
            m_session.get(), m_sftp.get(), source_string.data(),
            source_string.size(), destination_string.data(),
            destination_string.size(), flags);
    }

    /**
     * Remove a file.
     *
     * Removes `target` on the filesystem available via this channel.  If 
     * `target` is a symlink, only removes the link, not what the link
     * resolves to.  If `target` is a directory, removes it only if the
     * directory is empty.
     *
     * @returns `true` if the file was removed and `false` if the file did not
     *          exist in the first place.
     * @throws `boost::system::system_error` if `target` is a non-empty
     *         directory.
     *
     * If the calling code already knows whether `target` is a directory, this
     * function adds the overhead of a single extra stat call to the server above
     * what would be possible using plain SFTP unlink/rmdir.  This trip is needed
     * to find out that information and allows us to mirror the
     * POSIX/Boost.Filesystem remove functions that do not differentiate
     * directories.
     */
    bool remove(const boost::filesystem::path& target)
    {
        // Unlike the POSIX/Boost.Filesystem API we are following, the SFTP
        // protocol mirrors the C API where directories can only be removed
        // using the special RMDIR command.
        //
        // We tried to avoid an extra round trip to the server (to stat the
        // file) by blindly trying the common case of non-directories and
        // ignoring the first SFTP error.  The theory was that any real
        // error should also occur on the second (rmdir) attempt.
        // But that's not true because the second error might be complaining
        // that we're trying the wrong kind of delete while the first error
        // is the actual problem (permissions, for example).  Saving the first
        // error, and overwriting the second error with it, doesn't solve the
        // problem either as it could be the second error that gives the real
        // problem with the first error being wrong-kind-of-delete.  Basically
        // we can't know which error is 'real'.  If we did, we'd know the
        // filetype already!

        switch (detail::check_status(*this, target))
        {
        case detail::path_status::non_existent:
            return false;

        case detail::path_status::directory:
            return remove_empty_directory(target);

        case detail::path_status::non_directory:
            // This includes 'unknown' file type.  What's the alternative?
            return remove_one_file(target);

        default:
            assert(false);
            BOOST_THROW_EXCEPTION(std::logic_error("Unknown path status"));
            return 0U;
        }
    }

    /**
     * Remove a file and anything below it in the hierarchy.
     *
     * Removes `target` on the filesystem available via `channel`.  If `target` is
     * a symlink, only removes the link, not what the link resolves to.  If
     * `target` is a directory, removes it and all its contents.
     *
     * @returns the number of files removed.
     *
     * If the calling code already knows whether `target` is a directory, this
     * function adds the overhead of a single extra stat call to the server above
     * what would be possible using plain SFTP unlink/rmdir.  This trip is needed
     * to find out that information and allows us to mirror the
     * POSIX/Boost.Filesystem remove functions that do not differentiate
     * directories.
     *
     * All files below the target must be statted (indirectly via directory listing)
     * by any implementation so this function adds no overhead for those.
     */
    boost::uintmax_t remove_all(const boost::filesystem::path& target)
    {
        switch (detail::check_status(*this, target))
        {
        case detail::path_status::non_existent:
            return 0U;

        case detail::path_status::directory:
            return remove_directory(target);

        case detail::path_status::non_directory:
            // This includes 'unknown' file type.  What's the alternative?
            return remove_one_file(target);

        default:
            assert(false);
            BOOST_THROW_EXCEPTION(std::logic_error("Unknown path status"));
            return 0U;
        }
    }

    /**
     * Make a directory accessible from the given path.
     *
     * @returns `true` if a new directory was created at `new_directory`
     *          `false` if a directory already existed on that path.
     *
     * This function mirrors Boost.Filesystem `create_directory` except that
     * directories are created with 0755 permissions instead of 0777.  0755 is
     * more secure and the recommended permissions for directories on web server
     * so seems more appropriate.  It's not clear why Boost.Filesystem chooses
     * 0777 instead.
     */
    bool create_directory(const boost::filesystem::path& new_directory)
    {
        std::string new_directory_string = new_directory.string();

        try
        {
            ::ssh::detail::libssh2::sftp::mkdir_ex(
                m_session.get(), m_sftp.get(),
                new_directory_string.data(),
                new_directory_string.size(),
                LIBSSH2_SFTP_S_IRWXU |
                LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IXGRP |
                LIBSSH2_SFTP_S_IROTH | LIBSSH2_SFTP_S_IXOTH);

            return true;
        }
        catch (const boost::system::system_error&)
        {
            // Might just be because it already exists.  Let's check that and if
            // ignore if that's the case.
            // Doing this test after avoids an extra trip to the server in the
            // common case.

            // We don't test the exception's error code because OpenSSH just
            // returns FX_FAILURE which could have many causes.  The only way
            // to be sure the directory is already there is to check explicitly.

            switch (detail::check_status(*this, new_directory))
            {
            case detail::path_status::non_directory:
            case detail::path_status::non_existent:
                throw;

            case detail::path_status::directory:
                return false;

            default:
                assert(false);
                BOOST_THROW_EXCEPTION(std::logic_error("Unknown path status"));
                return false;
            }
        }
    }

private:

    friend class sftp_input_device;
    friend class sftp_output_device;
    friend class sftp_io_device;

    bool remove_one_file(const boost::filesystem::path& file)
    {
        return do_remove(file, false);
    }

    bool remove_empty_directory(const boost::filesystem::path& file)
    {
        return do_remove(file, true);
    }

    boost::uintmax_t remove_directory(const boost::filesystem::path& root);

    bool do_remove(
        const boost::filesystem::path& target, bool is_directory)
    {
        std::string target_string = target.string();

        try
        {
            if (is_directory)
            {
                ::ssh::detail::libssh2::sftp::rmdir_ex(
                    m_session.get(), m_sftp.get(),
                    target_string.data(), target_string.size());
            }
            else
            {
                ::ssh::detail::libssh2::sftp::unlink_ex(
                    m_session.get(), m_sftp.get(),
                    target_string.data(), target_string.size());
            }
        }
        catch (const boost::system::system_error& e)
        {
            if (e.code() == boost::system::errc::no_such_file_or_directory)
            {
                // Mirror the Boost.Filesystem API which doesn't treat this
                // as an error.
                return false;
            }
            else
            {
                throw;
            }
        }

        return true;
    }

    /**
     * Common parts of readlink and realpath.
     */
    boost::filesystem::path symlink_resolve(
        const char* path, unsigned int path_len, int resolve_action)
    {
        // yuk! hardcoded buffer sizes. unfortunately, libssh2 doesn't
        // give us a choice so we allocate massive buffers here and then
        // take measures later to reduce the footprint

        std::vector<char> target_path_buffer(1024, '\0');

        int len = ::ssh::detail::libssh2::sftp::symlink_ex(
            m_session.get(), m_sftp.get(), path, path_len,
            &target_path_buffer[0], target_path_buffer.size(),
            resolve_action);

        return boost::filesystem::path(
            &target_path_buffer[0], &target_path_buffer[0] + len);
    }

    boost::shared_ptr<LIBSSH2_SESSION> m_session;
    boost::shared_ptr<LIBSSH2_SFTP> m_sftp;
};

namespace detail {

    inline BOOST_SCOPED_ENUM(path_status) check_status(
        sftp_channel channel, const boost::filesystem::path& path)
    {
        try
        {
            file_attributes attrs = channel.attributes(path, false);

            if (attrs.type() == file_attributes::directory)
            {
                return path_status::directory;
            }
            else
            {
                return path_status::non_directory;
            }
        }
        catch (const boost::system::system_error& e)
        {
            // Process errors by catching the exception, rather than
            // intercepting the error code directly, so as not to
            // duplicate the exception info processing.

            if (e.code() == boost::system::errc::no_such_file_or_directory)
            {
                // Mirror the Boost.Filesystem API which doesn't treat
                // this as an error.
                return path_status::non_existent;
            }
            else
            {
                throw;
            }
        }
    }

}

/**
 * Does a file exist at the given path.
 */
inline bool exists(sftp_channel channel, const boost::filesystem::path& file)
{
    try
    {
        channel.attributes(file, false);
    }
    catch (const boost::system::system_error& e)
    {
        if (e.code() == boost::system::errc::no_such_file_or_directory)
        {
            return false;
        }
        else
        {
            throw;
        }
    }

    return true;
}

inline boost::filesystem::path resolve_link_target(
    sftp_channel channel, const sftp_file& link)
{
    return channel.resolve_link_target(link.path());
}

inline boost::filesystem::path canonical_path(
    sftp_channel channel, const sftp_file& link)
{
    return channel.canonical_path(link.path());
}

// Needs directory_iterator implementation so outside sftp_channel class body
inline boost::uintmax_t sftp_channel::remove_directory(
    const boost::filesystem::path& root)
{
    boost::uintmax_t count = 0U;

    for (ssh::sftp::directory_iterator directory = directory_iterator(root);
        directory != directory_iterator(); ++directory)
    {
        const sftp_file& file = *directory;

        if (file.name() == "." || file.name() == "..")
        {
            continue;
        }

        if (file.attributes().type() == file_attributes::directory)
        {
            count += remove_directory(file.path());
        }
        else
        {
            if (remove_one_file(file.path()))
            {
                ++count;
            }
            else
            {
                // Something else deleted the file before we could
            }
        }
    }

    if (remove_empty_directory(root))
    {
        ++count;
    }
    else
    {
        // Something else deleted the directory before we could or it
        // never existed in the first place
    }

    return count;
}

}} // namespace ssh::sftp

#endif
