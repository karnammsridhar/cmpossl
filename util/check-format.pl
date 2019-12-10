#!/usr/bin/perl
#
# Copyright 2019 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the Apache License 2.0 (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html
#
# check-format.pl
# - check formatting of C source according to OpenSSL coding style
#
# usage:
#   check-format.pl [-l|--sloppy-len] [-s|--sloppy-spc]
#                   [-c|--sloppy-cmt] [-m|--sloppy-macro]
#                   [-h|--sloppy-hang] [-1|--1-stmt]
#                   <files>
#
# checks adherence to the formatting rules of the OpenSSL coding guidelines.
# This pragmatic tool is incomplete and yields some false positives.
# Still it should be useful for detecting most typical glitches.
#
# options:
#  -l | --sloppy-len   increases accepted max line length from 80 to 84
#  -s | --sloppy-spc   disables reporting whitespace nits
#  -c | --sloppy-cmt   allows any indentation for comments
#  -c | --sloppy-macro allows missing extra indentation of macro bodies
#  -h | --sloppy-hang  when checking hanging indentation, suppresses reports for
#                      * same indentation as on line before
#                      * same indentation as non-hanging indent level
#                      * indentation moved left (not beyond non-hanging indent)
#                        just to fit contents within the line length limit
#  -1 | --1-stmt       do more aggressive checks for { 1 stmt } - see below
#
# There are non-triviel false positives and negatives such as the following.
#
# * There is the special OpenSSL rule not to unnecessarily use braces around
#   single statements:
#   {
#       stmt;
#   }
#   except within if .. else constructs where some branch contains more than one
#   statement. Since the exception is hard to recognize when such branches occur
#   after the current position (such that false positives would be reported)
#   the tool by checks for this rule by defaul only for do/while/for bodies.
#   Yet with the --1-stmt option false positives are preferred over negatives.
#   False negatives occur if the braces are more than two lines apart.
#
# * Use of multiple consecutive spaces is regarded a coding style nit except
#   when done in order to align certain columns over multiple lines, e.g.:
#   # define AB  1
#   # define CDE 22
#   # define F   3333
#   This pattern is recognized - and consequently double space not reported -
#   for a given line if in the nonempty line before or after (if existing)
#   for each occurrence of "  \S" (where \S means non-space) in the given line
#   there is " \S" in the other line in the respective column position.
#   This may lead to both false negatives (in case of coincidental " \S")
#   and false negatives (in case of more complex multi-column alignment).

use strict;
# use List::Util qw[min max];
use POSIX;

use constant INDENT_LEVEL => 4;
use constant MAX_LENGTH => 80;

# command-line options
my $max_length = MAX_LENGTH;
my $sloppy_SPC = 0;
my $sloppy_hang = 0;
my $sloppy_cmt = 0;
my $sloppy_macro = 0;
my $extended_1_stmt = 0;

while($ARGV[0] =~ m/^-(\w|-[\w\-]+)$/) {
    my $arg = $1; shift;
    if($arg =~ m/^(l|-sloppy-len)$/) {
        $max_length += INDENT_LEVEL;
    } elsif($arg =~ m/^(s|-sloppy-spc)$/) {
        $sloppy_SPC = 1;
    } elsif($arg =~ m/^(c|-sloppy-cmt)$/) {
        $sloppy_cmt = 1;
    } elsif($arg =~ m/^(m|-sloppy-macro)$/) {
        $sloppy_macro = 1;
    } elsif($arg =~ m/^(h|-sloppy-hang)$/) {
        $sloppy_hang = 1;
    } elsif($arg =~ m/^(1|-1-stmt)$/) {
        $extended_1_stmt = 1;
    } else {
        die("unknown option: $arg");
    }
}

my $self_test;
my $line;                  # current line number
my $line_before;           # number of previous not essentially empty line (containing at most whitespace and '\')
my $line_before2;          # number of not essentially empty line before previous not essentially empty line
my $contents;              # contents of current line
my $contents_before;       # contents of $line_before, if $line_before > 0
my $contents_before_;      # contents of $line_before after blinding comments etc., if $line_before > 0
my $contents_before2;      # contents of $line_before2, if $line_before2 > 0
my $contents_before_2;     # contents of $line_before2 after blinding comments etc., if $line_before2 > 0
my $multiline_string;      # accumulator for lines containing multi-line string
my $count;                 # number of leading whitespace characters (except newline) in current line, which
                           # should be $block_indent+hanging_offset+$local_offset or $expr_indent, respectively
my $count_before;          # number of leading whitespace characters (except line ending chars) in $contents_before
my $has_label;             # current line contains label
my $local_offset;          # current extra indent due to label, switch case/default, or leading closing brace(s)
my $line_opening_brace;    # number of previous line with opening brace after do/while/for, optionally for if/else
my $keyword_opening_brace; # name of previous keyword, used if $line_opening_brace != 0
my $ifdef__cplusplus;      # line before contained '#ifdef __cplusplus' (used in header files)
my $block_indent;          # currently required normal indentation at block/statement level
my $hanging_offset;        # extra indent, which may be nested, for just one hanging statement or expr or typedef
my @nested_block_indents;  # stack of indentations at block/statement level, needed due to hanging statements
my @nested_hanging_offsets;# stack of nested $hanging_offset values, in parallel to @nested_block_indents
my @nested_indents;        # stack of hanging indents due to parentheses, braces, brackets, or conditionals
my @nested_symbols;        # stack of hanging symbols '(', '{', '[', or '?', in parallel to @nested_indents
my @nested_conds_indents;  # stack of hanging indents due to conditionals ('?' .. ':')
my $expr_indent;           # resulting hanging indent within (multi-line) expressions including type exprs, else 0
my $hanging_symbol;        # character ('(', '{', '[', not: '?') responsible for $expr_indent, if $expr_indent != 0
my $in_expr;               # in expression after if/while/for/switch/return/enum/LHS of assignment
my $in_paren_expr;         # in parenthesized if/while/for condition and switch expression, if $expr_indent != 0
my $in_typedecl;           # nesting level of typedef/struct/union/enum
my $in_directive;          # number of lines so far within preprocessor directive, e.g., macro definition
my $directive_nesting;     # currently required indentation of preprocessor directive according to #if(n)(def)
my $directive_offset;      # indent offset within multi-line preprocessor directive, if $in_directive > 0
my $in_macro_header;       # number of open parentheses + 1 in (multi-line) header of #define, if $in_directive > 0
my $in_comment;            # number of lines so far within multi-line comment, or -1 when end is on current line
my $in_formatted_comment;  # in multi-line comment started with "/*-", which indicates/allows special formatting
my $comment_indent;        # comment indent, if $in_comment != 0
my $num_reports_line = 0;  # number of issues found on current line
my $num_reports = 0;       # total number of issues found
my $num_SPC_reports = 0;   # total number of whitespace issues found
my $num_indent_reports = 0;# total number of indentation issues found
my $num_nesting_issues = 0;# total number of syntax issues found during sanity checks

sub report_flexibly {
    my $line = shift;
    my $msg = shift;
    my $contents = shift;
    my $report_SPC = $msg =~ /SPC/;
    return if $report_SPC && $sloppy_SPC;

    print "$ARGV:$line:$msg:$contents" unless $self_test;
    $num_reports_line++;
    $num_reports++;
    $num_SPC_reports++    if $report_SPC;
    $num_indent_reports++ if $msg =~ m/indent/;
    $num_nesting_issues++ if $msg =~ m/unclosed|unexpected/;
}

sub report {
    my $msg = shift;
    report_flexibly($line, $msg, $contents);
}

sub parens_balance { # count balance of opening parentheses - closing parentheses
    my $str = shift;
    return $str =~ tr/\(// - $str =~ tr/\)//;
}

sub blind_nonspace { # blind non-space text of comment as @, preserving length
    # the @ character is used because it cannot occur in normal program code so there is no confusion
    # comment text is not blinded to whitespace in order to be able to check dbl SPC also in comments
    my $comment_text = shift;
    $comment_text =~ s/\.\s\s/.. /g; # in dbl SPC checks allow one extra space after period '.' in comments
    return $comment_text =~ tr/ /@/cr;
}

sub check_indent { # used for lines outside multi-line string literals
    if ($sloppy_cmt && substr($_, $count, 1) eq "@" && # line starting with comment
        ($in_comment == 0 || $in_comment == 1)) { # normal or first line of multi-line comment
        return;
    }

    if ($in_comment > 1 || $in_comment == -1) { # multi-line comment, but not on first line
        report("indent = $count != $comment_indent for multi-line comment") if $count != $comment_indent;
        return;
    }

    my $stmt_indent = $block_indent + $hanging_offset + $local_offset;
    my $stmt_desc = $contents =~
        m/^\s*\/\*/ ? "intra-line comment" :
        $has_label ? "label" :
        ($hanging_offset != 0 ? "hanging " : "").
        ($hanging_offset != 0 ? "stmt/expr" : "stmt/decl"); # $in_typedecl is not fully to the point here
    (my $ref_desc, my $ref_indent) = $expr_indent == 0 ? ($stmt_desc, $stmt_indent)
                                                       : ("hanging '$hanging_symbol'", $expr_indent);
    (my $alt_desc, my $alt_indent) = ("", $ref_indent);

    # allow indent 1 for labels - this cannot happen for leading ':'
    ($alt_desc, $alt_indent) = ("outermost position", 1) if $expr_indent == 0 && $has_label;

    if (@nested_conds_indents > 0 && substr($_, $count, 1) eq ":") {
        # leading ':' within stmt/expr/decl - this cannot happen for labels nor leading  '&&' or '||'
        # allow special indent at level of corresponding "?"
        ($alt_desc, $alt_indent) = ("leading ':'", @nested_conds_indents[-1]);
    }
    # allow extra indent offset leading '&&' or '||' - this cannot happen for leading ":"
    ($alt_desc, $alt_indent) = ("leading '$1'", $ref_indent + INDENT_LEVEL) if $contents =~ m/^\s*(\&\&|\|\|)/;

    if($expr_indent < 0) { # implies @nested_symbols > 0 && @nested_symbols[0] eq "{" && @nested_indents[-1] < 0
        # allow normal stmt indentation level for hanging initializer/enum expressions after '{'
        # this cannot happen for labels and overrides special treatment of ':', '&&' and '||' for this line
        ($alt_desc, $alt_indent) = ("lines after '{'", $stmt_indent);
        # decide depending on current actual indentation, preventing forth and back
        @nested_indents[-1] = $count == $stmt_indent ? $stmt_indent : -@nested_indents[-1]; # allow $stmt_indent
        $ref_indent = $expr_indent = @nested_indents[-1];
    }

    if ($sloppy_hang && ($hanging_offset != 0 || $expr_indent != 0)) {
        # do not report same indentation as on the line before (potentially due to same violations)
        return if $line_before > 0 && $count == $count_before;

        # do not report indentation at normal indentation level while hanging expression indent would be required
        return if $expr_indent != 0 && $count == $stmt_indent;

        # do not report if contents have been shifted left of nested expr indent (but not as far as stmt indent)
        # apparently aligned to the right in order to fit within line length limit
        return if $stmt_indent < $count && $count < $expr_indent && length($contents) == MAX_LENGTH + length("\n");
    }

    report("indent = $count != $ref_indent for $ref_desc".
           ($alt_desc eq ""
            || $alt_indent == $ref_indent # prevent showing alternative that happens to have equal value
            ? "" : " or $alt_indent for $alt_desc"))
        if $count != $ref_indent && $count != $alt_indent;
}

sub update_nested_indents { # may reset $in_paren_expr and in this case also resets $in_expr
    my $str = shift;
    my $start = shift; # defaults to 0
    my $terminator_position = -1;
    for(my $i = $start; $i < length($str); $i++) {
        my $c = substr($str, $i, 1);
        $c = ";" if substr($str, $i) =~ m/^\w*ASN1_[A-Z_]+END\w*/; # *ASN1_*END* macros are defined with leading ';'
        # stop at terminator outside 'for(..;..;..)', assuming that 'for' is followed by '('
        return $i if $c eq ";" && (!$in_paren_expr || @nested_indents == 0);

        my $in_stmt = $in_expr || @nested_symbols > 0;
        if ($c =~ m/[{([?]/) { # $c is '{', '(', '[', or '?'
            if ($c eq "{") { # '{' at block level but also inside stmt/expr/decl
                # cancel newly hanging_offset if opening brace '{' is after non-whitespace:
                $hanging_offset -= INDENT_LEVEL if $hanging_offset > 0 && substr($str, 0, $i) =~ m/\S/;
                push @nested_block_indents, $block_indent;
                push @nested_hanging_offsets, $in_expr ? $hanging_offset : 0;
                $block_indent += INDENT_LEVEL + $hanging_offset;
                $hanging_offset = 0;
            }
            if ($c ne "{" || $in_stmt) { # for '{' inside stmt/expr/decl, '(', '[', or '?'
                substr($str, $i + 1) =~ m/^(\s*)(\S)/;
                push @nested_indents, defined $2 ? $i + 1 + length $1 # actual indentation of following non-space
                                      : $c ne "{" ? $i + 1 # just after '(' or '[' if only whitespace thereafter
                    : -($i + 1); # allow also $stmt_indent if '{' with only whitespace thereafter
                push @nested_symbols, $c; # done also for '?' to be able to check correct nesting
                push @nested_conds_indents, $i if $c eq "?"; # remember special alternative indent for ':'
            }
        } elsif ($c =~ m/[})\]:]/) { # $c is '}', ')', ']', or ':'
            my $opening_c = ($c =~ tr/})]:/{([/r);
            if (($c ne ":" || $in_stmt    # ignore ':' outside stmt/expr/decl
                # in the presence of ':', one could add this sanity check:
                # && !(# ':' after initial label/case/default
                #      substr($str, 0, $i) =~ m/^(\s*)(case\W.*$|\w+$)/ || # this matching would not work for
                #                                                          # multi-line expr after 'case'
                #      # bitfield length within unsigned type decl
                #      substr($str, $i + 1) =~ m/^\s*\d+/                  # this matching would need to be improved
                #     )
                )) {
                if ($c ne "}" || $in_stmt) { # for '}' inside stmt/expr/decl, ')', ']', or ':'
                    if (@nested_symbols > 0 &&
                        @nested_symbols[-1] == $opening_c) { # for $c there was a corresponding $opening_c
                        pop @nested_indents;
                        pop @nested_symbols;
                        pop @nested_conds_indents if $opening_c eq "?";
                    } else {
                        report("unexpected '$c' @ ".($in_paren_expr ? "(expr)" : "expr"));
                        next;
                    }
                }
                if ($c eq "}") { # '}' at block level but also inside stmt/expr/decl
                    if (@nested_block_indents == 0) {
                        report("unexpected '}'");
                    } else {
                        $block_indent = pop @nested_block_indents;
                        $hanging_offset = pop @nested_hanging_offsets;
                    }
                }
                if($in_paren_expr && @nested_symbols == 0) {
                    $in_paren_expr = $in_expr = 0;
                    report("code after (expr)")
                        if substr($str, $i + 1) =~ m/^([^{]*)/ && $1 =~ m/[^\s;]/; # non-space non-; before any '{'
                }
            }
        }
    }
    return -1;
}

sub check_nested_nonblock_indents {
    my $position = shift;
    while(@nested_symbols != 0) {
        my $symbol = pop @nested_symbols;
        report("unclosed '$symbol' in $position");
        if ($symbol eq "{") { # repair stack of blocks
            $block_indent = pop @nested_block_indents;
            $hanging_offset = pop @nested_hanging_offsets;
        }
    }
    @nested_indents = ();
    @nested_conds_indents = ();
}

sub reset_file_state {
    $line = 0;
    $line_before = 0;
    $line_before2 = 0;
    @nested_block_indents = ();
    @nested_hanging_offsets = ();
    @nested_symbols = ();
    @nested_indents = ();
    @nested_conds_indents = ();
    $expr_indent = 0;
    $in_paren_expr = 0;
    $in_expr = 0;
    $hanging_offset = 0;
    $block_indent = 0;
    $ifdef__cplusplus = 0;
    undef $multiline_string;
    $line_opening_brace = 0;
    $in_typedecl = 0;
    $in_directive = 0;
    $directive_nesting = 0;
    $in_comment = 0;
    $in_formatted_comment = 0;
}

reset_file_state();

while(<>) { # loop over all lines of all input files
    $self_test = $ARGV =~ m/check-format-test.c$/;
    $line++;
    s/\r$//; # strip any trailing CR (which are typical on Windows systems)
    $contents = $_;

    # check for illegal characters
    if (m/(.*?)([\x00-\x09\x0B-\x1F\x7F-\xFF])/) {
        my $col = length $1;
        report(($2 eq "\x09" ? "TAB" : $2 eq "\x0D" ? "CR " : $2 =~ m/[\x00-\x1F]/ ? "non-printable"
                : "non-7bit char") . " at column $col") ;
    }

    # check for whitespace at EOL
    report("whitespace at EOL") if m/\s\n$/;

    # assign to $count the actual indentation level of the current line
    chomp; # remove trailing \n
    m/^(\s*)/;
    $count = length $1;
    $has_label = 0;
    $local_offset = 0;

    # comments and character/string literals @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

    # blind contents of character and string literals as @, preserving length
    # this prevents confusing any of the matching below, e.g., on SPC and comment delimiters
    s/\\"/@@/g; # blind all '\"' (typically within character literals or string literals)
    s#("[^"]*")#$1 =~ tr/"/@/cr#eg;
    s#('[^']*')#$1 =~ tr/'/@/cr#eg;
    # note that multi-line string literals are handled below

    # do/prepare checks within multi-line comments
    my $self_test_exception = $self_test ? "@" : "";
    if($in_comment > 0) { # this still includes the last line of multi-line commment
        m/^(\s*)(.?)(.*)$/;
        my ($head, $any_symbol, $cmt_text) = ($1, $2, $3);
        if($any_symbol eq "*") {
            report("no SPC after leading '*' in multi-line comment") if $cmt_text =~ m|^[^/\s$self_test_exception]|;
        } else {
            report("no leading '*' in multi-line comment");
        }
        $in_comment++;
    }

    # detect end of comment, must be within multi-line comment, check if it is preceded by non-whitespace text
    if (m/^(.*?)\*\/(.*)$/ && $1 ne '/') { # ending comment: '*/'
        my ($head, $tail) = ($1, $2);
        report("no SPC nor '*' before '*/'") if $head =~ m/[^\s*]$/;
        report("no SPC nor alphanumeric char after '*/'") if $tail =~ m/^\w/;
        if (!($head =~ m/\/\*/)) { # not starting comment '/*', which is is handled below
            if ($in_comment == 0) {
                report("unexpected '*/' outside comment");
                $_ = "$head@@".$tail; # blind the "*/"
            } else {
                report("non-SPC text before '*/' in multi-line comment") if $head =~ m/\S/; # head contains non-space
                my $cmt_text = $head;
                $_ = blind_nonspace($cmt_text)."@@".$tail;
                $in_comment = -1; # indicate that multi-line comment ends on current line
            }
        }
    }

    # detect start of comment, check if it is followed by non-space text
  MATCH_COMMENT:
    if (m/^(.*?)\/\*(-?)(.*)$/) { # starting comment: '/*'
        my ($head, $opt_minus, $tail) = ($1, $2, $3);
        report("no SPC before '/*'") if $head =~ m/[^\s*]$/; # no space before comment start delimiter;
                                                             # a '-' is allowed anyway due to the above matching
        report("no SPC nor '*' after '/*' or '/*-'") if $tail =~ m/^[^\s*$self_test_exception]/;
        my $cmt_text = $opt_minus.$tail; # preliminary
        if ($in_comment > 0) {
            report("unexpected '/*' inside multi-line comment");
        } elsif ($tail =~ m/^(.*?)\*\/(.*)$/) { # comment end: */ on same line
            report("unexpected '/*' inside intra-line comment") if $1 =~ /\/\*/;
            # blind comment text, preserving length
            ($cmt_text, my $rest) = ($opt_minus.$1, $2);
            if ($head =~ m/\S/ && # not leading comment: non-whitespace before
                $rest =~ m/^\s*\\?\s*$/) { # trailing comment: only whitespace (apart from any '\') after it
                report("dbl SPC in intra-line comment") if $opt_minus ne "-" && $cmt_text =~ m/(^|[^.])\s\s\S/;
                # blind trailing comment as space - TODO replace by @ after improving matching of trailing items
                $_ = "$head  ".($cmt_text =~ tr/ / /cr)."  $rest";
            } else { # leading or intra-line comment
                $_ = "$head@@".blind_nonspace($cmt_text)."@@".$rest;
            }
            goto MATCH_COMMENT;
        } else { # start of multi-line comment
            report("non-SPC text after '/*' in multi-line comment") unless $tail =~ m/^.?\s*\\?\s*$/;
                           # tail not essentially empty, first char already checked
            # adapt to actual indentation of first line
            $comment_indent = length($head) + 1;
            $_ = "$head@@".blind_nonspace($cmt_text);
            $in_comment = 1;
            $in_formatted_comment = $opt_minus eq "-";
        }
    }

    if($in_comment > 1) { # still inside multi-line comment (not at its start or end)
        m/^(\s*)\*?(\s*)(.*)$/;
        $_ = $1."@".$2.blind_nonspace($3);
    }

    # handle special case of line after '#ifdef __cplusplus' (which typically appears in header files)
    if ($ifdef__cplusplus) {
        $ifdef__cplusplus = 0;
        $_ = "$1 $2" if $contents =~ m/^(\s*extern\s*"C"\s*)\{(\s*)$/; # ignore opening brace in 'extern "C" {'
        goto LINE_FINISHED if m/^\s*\}\s*$/; # ignore closing brace '}'
    }

    # check for over-long lines,
    # while allowing trailing (also multi-line) string literals to go past $max_length
    my $len = length; # total line length (without trailing \n)
    if($len > $max_length &&
       !(m/^(.*?)"[^"]*("|\\)\s*(,|[\)\}]*[,;]?)\s*$/ && length($1) < $max_length)
        # this allows over-long trailing string literal with starting col before $max_length
        ) {
        report("line length = $len > $max_length");
    }

    # handle C++ / C99 - style end-of-line comments
    if(m|(.*?)//(.*$)|) {
        report("'//' end-of-line comment");  # the '//' comment style is not allowed for C90
        report("dbl SPC in end-of-line comment") if $2 =~ m/(^|[^.])\s\s\S/;
        # sacrifycing multi-line column alignment for this line -
        # TODO blind by @ after improving matching of trailing items
        $_ = $1; # anyway ignore comment text (not preserving length)
    }

    # at this point comment text has been removed/ignored (after checking dbl SPC)
    # or at least the non-space portions of commment text have been blinded as @

    goto LINE_FINISHED if m/^\s*\\?\s*$/; # essentially empty line (just whitespace any maybe a single backslash)

    # intra-line whitespace nits @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

    if(!$sloppy_SPC && !$in_formatted_comment) {
        my $dbl_SPC = "dbl SPC".($in_comment != 0 ? " in multi-line comment" : "");
        sub split_line_head {
            my $comment_symbol =
                $in_comment != 0 ? "@" : ""; # '@' will match the blinded leading '*' in multi-line comment
                                             # $in_comment may pertain to the following line due to delayed check
            # do not check for dbl SPC in leading spaces including any '#' (or '*' within multi-line comment)
            shift =~ m/^(\s*([#$comment_symbol]\s*)?)(.*?)\s*$/;
            return ($1, $3 =~ s/\s*\\\s*$//r); # strip any trailing '\' (and any whitespace around it)
        }
        my ($head , $intra_line ) = split_line_head($_);
        my ($head1, $intra_line1) = split_line_head($contents_before_ ) if $line_before > 0;
        my ($head2, $intra_line2) = split_line_head($contents_before_2) if $line_before2 > 0;
        if($line_before > 0) { # check with one line delay, such that at least $contents_before is available
            sub column_alignments_only {
                my $head = shift;
                my $intra = shift;
                my $contents = shift;
                # check if all dbl SPC in $intra is used only for multi-line column alignment with $contents
                my $offset = length($head);
                for(my $col = 0; $col < length($intra) - 2; $col++) {
                   return 0 if substr($intra   , $col, 3) =~ m/\s\s\S/ # double space (after leading space)
                          && !(substr($contents, $col + $offset + 1, 2) =~ m/\s\S/)
                }
                return 1;
            }
            report_flexibly($line_before, $dbl_SPC, $contents_before) if $intra_line1 =~ m/\s\s\S/ && !
                (    column_alignments_only($head1, $intra_line1, $_                )    # compare with $line
                 || ($line_before2 > 0 &&
                     column_alignments_only($head1, $intra_line1, $contents_before_2))); # compare with $line_before2
            report($dbl_SPC) if $intra_line  =~ m/\s\s\S/ && eof
                && ! column_alignments_only($head , $intra_line , $contents_before_ )  ; # compare with $line_before
        } elsif(eof) { # special case: just one line exists
            report($dbl_SPC) if $intra_line  =~ m/\s\s\S/;
        }
        # ignore paths in #include
        $intra_line =~ s/^(include\s*)(".*?"|<.*?>)/$1/e if $head =~ m/#/;
        # treat op= and comparison operators as simple '=', simplifying matching below
        $intra_line =~ s/([\+\-\*\/\/%\&\|\^\!<>=]|<<|>>)=/=/g;
        # treat (type) variables within macro, indicated by trailing '\', as 'int' simplifying matching below
        $intra_line =~ s/[A-Z_]+/int/g if $contents =~ m/^(.*?)\s*\\\s*$/;
        # treat double &&, ||, <<, and >> as single ones, simplifying matching below
        $intra_line =~ s/(&&|\|\||<<|>>)/substr($1,0,1)/eg;
        # remove blinded comments etc. directly before ,;)}
        while($intra_line =~ s/\s*@+([,;)}\]])/$1/e) {} # /g does not work here
        # treat remaining blinded comments and string literals as (single) space during matching below
        $intra_line =~ s/@+/ /g; # note that dbl SPC has already been handled above
        $intra_line =~ s/\s+$//;                     # strip any (resulting) space at EOL
        $intra_line =~ s/(for\s*\();;(\))/"$1$2"/eg; # strip ';;' in for (;;)
        $intra_line =~ s/(=\s*)\{ /"$1@ "/eg;        # do not report {SPC in initializers such as ' = { 0, };'
        $intra_line =~ s/, \};/, @;/g;               # do not report SPC} in initializers such as ' = { 0, };'
        $intra_line =~ s/\-\>|\+\+|\-\-/@/g;         # blind '->,', '++', and '--'
        $intra_line =~ s/:\s;/:;/g;                  # strip any SPC between 'label:' and ';'
        report("SPC before '$1'")     if $intra_line =~ m/\s([,;)\]])/;      # space before ,;)]
        report("SPC after '$1'")      if $intra_line =~ m/([(\[])\s/;        # space after ([
        report("no SPC before '$1'")  if $intra_line =~ m/\S([=|+\/%<>])/;   # =|+/%<> without preceding space
        report("no SPC before '$1'")  if $intra_line =~ m/[^\s()]([-])/;     # '-' without preceding space or '('
                                                                             # or ')' (which is used for type casts)
        report("no SPC before '$1'")  if $intra_line =~ m/[^\s{()\[*]([*])/; # '*' without preceding space or {()[*
        report("no SPC before '$1'")  if $intra_line =~ m/[^\s{(\[]([&])/;   # '&' without preceding space or {([
        report("no SPC after '$1'")   if $intra_line =~ m/([,;=|\/%<>])\S/;  # ,;=|/% without following space
        report("no SPC nor name after '$1'") if $intra_line=~m/([*])[^\s\w()*]/; # '*' w/o following space or \w()*
        report("no SPC nor name after '$1'") if $intra_line=~m/([&])[^\s\w]/;    # '&' without following space or \w
        report("no SPC nor digit after '$1'")if $intra_line=~m/([+\-])[^\s\d]/;  # +- without following space or \d
        report("no SPC after '$2'")   if $intra_line =~ m/(^|\W)(if|while|for|switch)[^\w\s]/;  # if etc. w/o SPC
        report("SPC after function/macro name")
                                      if $intra_line =~ m/(\w+)\s+\(/        # fn/macro name with space before '('
       && !($1 =~ m/^(if|while|for|switch|return|typedef|void|char|unsigned|int|long|float|double)$/) # not: keyword
                                    && !(m/^\s*#\s*define\s/); # we skip macro definitions here because macros
                                    # without parameters but with body starting with '(', e.g., '#define X (1)',
                                    # would lead to false positives - TODO also check for macros with parameters
        report("no SPC before '{'")   if $intra_line =~ m/[^\s{(\[]\{/;      # '{' without preceding space or {([
        report("no SPC after '}'")    if $intra_line =~ m/\}[^\s,;\])}]/;    # '}' without following space or ,;])}
    }

    # preprocessor directives and characters/string iterals @@@@@@@@@@@@@@@@@@@@

    # handle preprocessor directives
    if (m/^\s*#(\s*)(\w+)/) { # line starting with '#'
        my $space_count = length $1; # maybe could also use indentation before '#'
        my $directive = $2;
        report("indent = $count != 0 for '#'") if $count != 0;
        $directive_nesting-- if $directive =~ m/^(else|elif|endif)$/;
        if ($directive_nesting < 0) {
            $directive_nesting = 0;
            report("unexpected '#$directive'");
        }
        report("'#' directive nesting = $space_count != $directive_nesting") if $space_count != $directive_nesting;
        $directive_nesting++ if $directive =~ m/^if|ifdef|ifndef|else|elif$/;
        $ifdef__cplusplus = m/^\s*#\s*ifdef\s+__cplusplus\s*$/;
        goto POSTPROCESS_DIRECTIVE unless $directive =~ m/^define$/; # skip normal code handling except for #define
        # TODO improve current mix of handling indents for normal C code and preprocessor directives
    }

    # handle multi-line string literals to avoid confusion on trailing '\' -
    # this is not done for other uses of trailing '\' in order to be able
    # to check layout of multi-line preprocessor directives
    if (defined $multiline_string) {
        $_ = $multiline_string.$_;
        undef $multiline_string;
        m/^(\s*)/; $count = length $1; # re-calculate count, like done above
    }
    if (m/^(([^"]*"[^"]*")*[^"]*"[^"]*)\\\s*$/) { # trailing '\' in last string literal
        $multiline_string = $1;
        goto LINE_FINISHED; # TODO check indents not only for first line of multi-line string
    }

    # trailing '\' is typically used in multi-line macro definitions;
    # strip it along with any preceding whitespace such that it does not interfere with various matching done below
    $_ = $1 if (m/^(.*?)\s*\\\s*$/); # trailing '\'

    # adapt required indentation @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

    # adapt $local_offset and $expr_indent according to leading closing brace(s) '}' or label or switch case
    my $in_stmt = $in_expr || @nested_symbols > 0;
    if ($in_stmt) { # expr/stmt/type decl/var def/fn hdr, i.e., not at block level
        if(m/^\s*\}/) { # leading '}', any preceding blinded comment must not be matched
            if($in_expr && @nested_symbols == 1 && @nested_symbols[-1] == "{") { # end of initialiizer expr or enum
                $block_indent = @nested_block_indents[-1];     # cannot be empty due to ...
                $hanging_offset = @nested_hanging_offsets[-1]; # @nested_symbols == 1 && @nested_symbols[-1] == "{"
                $expr_indent = 0;
            } elsif (@nested_symbols >= 2) { # @nested_symbols >= 2 usually is the case at this point anyway
                $hanging_symbol = @nested_symbols[-2];
                $expr_indent = @nested_indents[-2];
            }
        } elsif(m/^(\s*)(static_)?ASN1_ITEM_TEMPLATE_END(\W|$)/) { # special handling for ASN1 macro indented as '}'
            $local_offset -= INDENT_LEVEL;
            $expr_indent = 0;
        }
    } else { # at block level, i.e., outside expr/stmt/type decl/var def/fn hdr
        report("code before '}'") if m/^\s*[^\s{}][^{}]*\}/; # non-whitespace non-} before first '}'
        if(@nested_block_indents > 0 &&
           m/^\s*\}/) { # leading '}', any preceding blinded comment must not be matched
            $local_offset -= INDENT_LEVEL;
        }
        if (m/^\s*(case|default)(\W|$)/) {
            $local_offset = -INDENT_LEVEL;
        } else {
            if (m/^(\s*)(\w+):/) { # label, cannot be "default"
                $local_offset = -INDENT_LEVEL + 1 ;
                $has_label = 1;
            }
        }
    }

    # potential adaptations of indent in first line of macro body in multi-line macro definition
    if ($in_directive > 0 && $in_macro_header > 0) {
        if ($in_macro_header > 1) { # still in macro definition header
            $in_macro_header += parens_balance($_);
        } else { # start of macro body
            $in_macro_header = 0;
            if ($count == $block_indent - $directive_offset # body started with same indentation as preceding code
                && $sloppy_macro) { # workaround for this situation is enabled
                $block_indent -= $directive_offset;
                $directive_offset = 0;
            }
        }
    }

    # check required indentation @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

    check_indent() unless $contents =~ m/^\s*#\s*define(\W|$)/; # indent of #define has been handled above

    $in_comment = $in_formatted_comment = 0 if $in_comment == -1; # multi-line comment has ended

    # do some further checks @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

    my $outermost_level = $block_indent == 0 + ($in_directive > 0 ? $directive_offset : 0);

    report("more than one stmt") if !m/(^|\W)for(\W.*|$)/ && # no 'for' - TODO improve matching
        m/;.*;/; # two or more terminators ';', so more than one statement

    # check for code block containing a single line/statement
    if($line_before2 > 0 && !$outermost_level && # within function body
       $in_typedecl == 0 && @nested_indents == 0 && # not within type declaration nor inside stmt/expr
       m/^\s*\}/) { # leading closing brace '}', any preceding blinded comment must not be matched
        # TODO extend detection from single-line to potentially multi-line statement
        if($line_opening_brace != 0 &&
           ($line_opening_brace == $line_before2 ||
            $line_opening_brace == $line_before)
           && $contents_before =~ m/;/) { # there is at least one terminator ';', so there is some stmt
            # TODO do not report cases where a further else branch
            # follows with a block containg more than one line/statement
            report_flexibly($line_before, "'$keyword_opening_brace' { 1 stmt }", $contents_before);
        }
    }

    # TODO report empty line within local variable definitions

    # TODO report missing empty line after local variable definitions

    # TODO report needless use of parentheses, while
    #      macro parameters should always be in parens, e.g., '#define ID(x) (x)'

    # adapt required indentation for following lines @@@@@@@@@@@@@@@@@@@@@@@@@@@

    my $update_nested_indents_start = 0; # this will be changed for handling expressions starting within the line

    # set $in_expr, $in_paren_expr, and $hanging_offset for if/while/for/switch, return/enum, and assignment RHS
    my $paren_expr_start = 0;
    my $return_enum_start = 0;
    my $assignment_start = 0;
    s/[\!<>=]=/@@/g; # blind (in-)equality symbols like '<=' as '@@' to prevent matching them as '=' below
    if      (m/^((^|.*\W)(if|while|for|switch))(\W.*|$)$/) { # (first) if/for/while/switch
        $paren_expr_start = 1;
    } elsif (m/^((^|.*\W)(return|enum))(\W.*|$)/) {          # (first) return/enum
        $return_enum_start = 1;
    } elsif (m/^(([^=]*)(=))(.*)$/                           # (first) '=', i.e., assignment
        && !$in_expr && @nested_indents == 0 && parens_balance($1) == 0) { # not nested assignment
        $assignment_start = 1;
    }
    if ($paren_expr_start || $return_enum_start || $assignment_start)
    {
        (my $head, my $mid, my $tail) = ($1, $3, $4);
        $keyword_opening_brace = $mid if $mid ne "=" && $tail =~ m/\{/;

        # already handle $head, i.e., anything before expression
        update_nested_indents($head);
        $update_nested_indents_start = length $head;
        # now can set $in_expr and $in_paren_expr
        $in_expr = 1;
        $in_paren_expr = 1 if $paren_expr_start;
        $hanging_offset += INDENT_LEVEL; # this will be canceled for 'while' that terminates a 'do ... while'
                                         # because in this case the 'while' is terminated by a ';'
    }

    # set $hanging_offset for do/else
    if (!$in_paren_expr && !$return_enum_start && m/(^|\W)(else|do)(\W.*|$)$/) {
        (my $mid, my $tail) = ($2, $3);
        $keyword_opening_brace = $mid if $tail =~ m/\{/;
        $hanging_offset += INDENT_LEVEL;
    }

    if (m/(^|\W)(typedef|struct|union|enum)(\W.*|$)$/) { # type declaration
        # not needed: $keyword_opening_brace = $2 if $3 =~ m/\{/;
        $in_typedecl++;
        $hanging_offset += INDENT_LEVEL if m/\*.*\(/; # '*' followed by '(' - seems consistent with Emacs C mode
    }

    my ($bak_in_expr, $bak_in_paren_expr) = ($in_expr, $in_paren_expr);
    my $terminator_position = update_nested_indents($_, $update_nested_indents_start);

    if ($bak_in_paren_expr) { # expression in parentheses after if/while/for/switch
        if (!$in_paren_expr) { # end of its (expr)
            check_nested_nonblock_indents("(expr)");
        }
    } elsif ($bak_in_expr) {
        # on end of non-if/while/for/switch (multi-line) expression (i.e., return/enum/assignment) and
        # on end of statement/type declaration/variable definition/function header
        if ($terminator_position >= 0 && ($in_typedecl == 0 || @nested_indents == 0)) {
            check_nested_nonblock_indents("expr");
            $in_expr = 0;
        }
    } else {
        check_nested_nonblock_indents($in_typedecl == 0 ? "stmt" : "decl") if $terminator_position >= 0;
    }

    # on ';', which terminates the current statement/type declaration/variable definition/function declaration
    if ($terminator_position >= 0) {
        $hanging_offset = 0; # normal end, or cancel after 'do .. while'
        $in_typedecl-- if $in_typedecl > 0; # TODO also handle multiple type decls per line
        m/(;[^;]*)$/; # match last ';'
        $terminator_position = length($_) - length($1) if $1;
        # new $terminator_position value may be after the earlier one in case multiple terminators on current line
        # TODO check treatment in case of multiple terminators on current line
        update_nested_indents($_, $terminator_position + 1);
    }

    # set hanging expression indent according to nested indents
    # also if $in_expr is 0: in statement/type declaration/variable definition/function header
    $expr_indent = 0;
    for (my $i = -1; $i >= -@nested_symbols; $i--) {
        if (@nested_symbols[$i] ne "?") { # conditionals '?' ... ':' are treated specially in check_indent()
            $hanging_symbol = @nested_symbols[$i];
            $expr_indent = $nested_indents[$i];
            # $expr_indent is guaranteed to be != 0 unless @nested_indents contains just outer conditionals
            last;
        }
    }

    # special checks for last, typically trailing opening brace '{' in line
    if (m/^(.*?)\{([^\{]*)$/) { # match last ... '{'
        my ($head, $tail) = ($1, $2);
        if ($in_directive == 0 && !$in_expr && $in_typedecl == 0) {
            if ($outermost_level) {
                if (!$assignment_start &&
                    # at end of function definition header (or stmt or var definition)
                    !($head =~ m/^$/)) { # check if opening brace '{' is at the beginning of the next line
                    report("'{' not at line start");
                }
            } else {
                $line_opening_brace = $line if $keyword_opening_brace =~ m/do|while|for/;
                # using, not assigning, $keyword_opening_brace here because it could be on an earlier line
                $line_opening_brace = $line if $keyword_opening_brace =~ m/if|else/ && $extended_1_stmt &&
                # TODO prevent false positives for if/else where braces around single-statement branches
                # should be avoided but only if all branches have just single statements
                # The following helps detecting the exception when handling multiple 'if ... else' branches:
                    !($keyword_opening_brace eq "else" && $line_opening_brace < $line_before2);
            }
            report("code after '{'") if $tail=~ m/\S/ && # non-whitespace
                                      !($tail=~ m/\}/);  # no '}' after last '{'
        }
    }

  POSTPROCESS_DIRECTIVE:
    # on start of multi-line preprocessor directive, adapt indent
    # need to use original line contents because trailing '\' may have been stripped above
    if ($contents =~ m/^(.*?)\s*\\\s*$/) { # trailing '\',
        # typically used in macro definitions (or other preprocessor directives)
        if ($in_directive == 0) {
            $in_macro_header = m/^\s*#\s*define(\W|$)?(.*)/ ? 1 + parens_balance($2) : 0; # #define is starting
            $directive_offset = INDENT_LEVEL;
            $block_indent += $directive_offset;
        }
        $in_directive += 1;
    }

  LINE_FINISHED:
    # on end of multi-line preprocessor directive, adapt indent
    if ($in_directive > 0 &&
        # need to use original line contents because trailing \ may have been stripped
        !($contents =~ m/^(.*?)\s*\\\s*$/)) { # no trailing '\'
        $block_indent -= $directive_offset;
        $in_directive = 0;
        # macro body typically does not include terminating ';'
        $hanging_offset = 0; # compensate for this in case macro ends, e.g., as 'while (0)'
    }

    unless (m/^\s*\\?\s*$/) { # essentially empty line (just whitespace any maybe a single backslash)
        $line_before2      = $line_before;
        $contents_before2  = $contents_before;
        $contents_before_2 = $contents_before_;
        $line_before       = $line;
        $contents_before   = $contents;
        $contents_before_  = $_;
        $count_before      = $count;
    }

    if($self_test) { # debugging
        my $should_report = $contents =~ m/\*@(\d)?/ ? 1 : 0;
        $should_report = +$1 if defined $1;
        print("$ARGV:$line:$num_reports_line reports on:$contents")
            if $num_reports_line != $should_report;
    }
    $num_reports_line = 0;

    # post-processing at end of file @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

    if(eof) {
        # check for essentially empty line (which may include a single '\\') just before EOF
        report(($contents eq "\n" ? "empty line" : $2 ne "" ? "'\\'" : "whitespace").
               " at EOF") if $contents =~ m/^(\s*(\\?)\s*)$/;

        # report unclosed expression-level nesting
        check_nested_nonblock_indents("expr at EOF"); # also adapts @nested_block_indents

        # sanity-check balance of block-level { .. } via final $block_indent at end of file
        report_flexibly($line, +@nested_block_indents." unclosed '{'", "(EOF)\n") if @nested_block_indents > 0;

        # sanity-check balance of #if .. #endif via final preprocessor directive indent at end of file
        report_flexibly($line, "$directive_nesting unclosed '#if'", "(EOF)\n") if $directive_nesting != 0;

        reset_file_state();
    }
}

my $num_other_reports = $num_reports - $num_indent_reports - $num_SPC_reports;
print "$num_reports ($num_indent_reports indentation, $num_SPC_reports whitespace, ".
    "$num_nesting_issues nesting, $num_other_reports other) issues ".
    "have been found by $0\n" unless $self_test;
