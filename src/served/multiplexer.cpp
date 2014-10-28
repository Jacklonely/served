/*
 * Copyright (C) 2014 MediaSift Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <served/status.hpp>
#include <served/request_error.hpp>
#include <served/multiplexer.hpp>
#include <served/mux/matchers.hpp>

namespace served {

//  -----  constructors  -----

multiplexer::multiplexer()
	: _base_path("")
{
}

multiplexer::multiplexer(const std::string & base_path)
	: _base_path(base_path)
{
}

//  -----  plugin injection  -----

void
multiplexer::use_plugin(served_plugin_req_handler plugin)
{
	_plugin_handlers.push_back(plugin);
}

//  -----  path parsing  -----

std::vector<std::string>
split_path(const std::string & path)
{
	std::vector<std::string> chunks;

	const char * path_ptr = path.c_str();
	const size_t path_len = path.length();

	char   tmp[path.length()];
	char * end = tmp;

	const char * const eol = path_ptr + path_len;

	for (; path_ptr < eol; ++path_ptr)
	{
		if ( '/' ==  *path_ptr )
		{
			if ( end != tmp )
			{
				chunks.push_back(std::string(tmp, end));
				end = tmp;
			}
		}
		else
		{
			*end++ = *path_ptr;
		}
	}

	if ( end != tmp )
	{
		chunks.push_back(std::string(tmp, end));
	}
	else if ( '/' == path[path.length() - 1] )
	{
		// Push back an empty string for paths ending in /
		chunks.push_back("");
	}

	return chunks;
}

multiplexer::path_compiled_segments
multiplexer::get_segments(const std::string & path)
{
	path_compiled_segments segments;

	for ( const auto & chunk : split_path(path) )
	{
		segments.push_back(mux::compile_to_matcher(chunk));
	}

	return segments;
}

//  -----  http request handlers  -----

served::methods_handler &
multiplexer::handle(const std::string & path)
{
	_handler_candidates.push_back(
		path_handler_candidate(get_segments(path), served::methods_handler()));

	return std::get<1>(_handler_candidates.back());
}

//  -----  request forwarding  -----

void
multiplexer::forward_to_handler(served::response & res, served::request & req)
{
	bool pattern_matched = false;

	// Split request path into segments
	const auto request_segments = split_path(req.url().path());
	const int  r_size           = request_segments.size();

	// For each candidate
	for ( const auto & candidate : _handler_candidates )
	{
		// Get its segments
		const auto & handler_segments = std::get<0>(candidate);
		const int    h_size           = handler_segments.size();

		// If the candidate segment count does not match then skip
		if ( h_size != r_size )
		{
			continue;
		}

		// Check if each segment matches
		int seg_index = 0;
		for (; seg_index < h_size; seg_index++ )
		{
			if ( ! handler_segments[seg_index]->check_match(request_segments[seg_index]) )
			{
				break;
			}
		}

		// If all segments were matched then we have our chosen candidate
		if ( seg_index == h_size )
		{
			pattern_matched = true;

			// Check that the request method is supported by this candidate
			auto method_handler = std::get<1>(candidate);
			if ( ! method_handler.method_supported( req.method() ) )
			{
				throw served::request_error(served::status_4XX::METHOD_NOT_ALLOWED, "Method not allowed");
			}

			method_handler[ req.method() ](res, req);
			break;
		}
	}

	// If no candidates were matched then we throw a 404
	if ( ! pattern_matched )
	{
		throw served::request_error(served::status_4XX::NOT_FOUND, "Path not found");
	}
}

} // served
