$version: "2.0"

namespace com.example.petstore

use aws.protocols#restJson1

/// A simple pet store service for managing pets
@restJson1
service PetStore {
    version: "2024-01-01"
    operations: [
        GetPet
        ListPets
        CreatePet
        UpdatePet
        DeletePet
    ]
    errors: [
        ValidationError
        NotFoundError
        InternalServerError
    ]
}

/// Get a pet by ID
@http(method: "GET", uri: "/pets/{petId}", code: 200)
@readonly
operation GetPet {
    input: GetPetInput
    output: GetPetOutput
    errors: [NotFoundError]
}

/// List all pets with optional filtering
@http(method: "GET", uri: "/pets", code: 200)
@readonly
@paginated(inputToken: "nextToken", outputToken: "nextToken", pageSize: "maxResults", items: "pets")
operation ListPets {
    input: ListPetsInput
    output: ListPetsOutput
}

/// Create a new pet
@http(method: "POST", uri: "/pets", code: 201)
operation CreatePet {
    input: CreatePetInput
    output: CreatePetOutput
    errors: [ValidationError]
}

/// Update an existing pet
@http(method: "PUT", uri: "/pets/{petId}", code: 200)
@idempotent
operation UpdatePet {
    input: UpdatePetInput
    output: UpdatePetOutput
    errors: [NotFoundError, ValidationError]
}

/// Delete a pet
@http(method: "DELETE", uri: "/pets/{petId}", code: 204)
@idempotent
operation DeletePet {
    input: DeletePetInput
    output: DeletePetOutput
    errors: [NotFoundError]
}

// Input/Output structures

@input
structure GetPetInput {
    @required
    @httpLabel
    petId: String
}

@output
structure GetPetOutput {
    @required
    pet: Pet
}

@input
structure ListPetsInput {
    @httpQuery("species")
    species: PetSpecies

    @httpQuery("status")
    status: PetStatus

    @httpQuery("maxResults")
    maxResults: Integer

    @httpQuery("nextToken")
    nextToken: String
}

@output
structure ListPetsOutput {
    @required
    pets: PetList

    nextToken: String

    totalCount: Integer
}

@input
structure CreatePetInput {
    @required
    name: String

    @required
    species: PetSpecies

    age: Integer

    tags: TagList

    attributes: AttributeMap
}

@output
structure CreatePetOutput {
    @required
    pet: Pet
}

@input
structure UpdatePetInput {
    @required
    @httpLabel
    petId: String

    name: String

    species: PetSpecies

    status: PetStatus

    age: Integer

    tags: TagList
}

@output
structure UpdatePetOutput {
    @required
    pet: Pet
}

@input
structure DeletePetInput {
    @required
    @httpLabel
    petId: String
}

@output
structure DeletePetOutput {}

// Domain types

/// A pet in the store
structure Pet {
    @required
    id: String

    @required
    name: String

    @required
    species: PetSpecies

    @required
    status: PetStatus

    age: Integer

    tags: TagList

    attributes: AttributeMap

    @required
    createdAt: Timestamp

    updatedAt: Timestamp
}

/// Tag for categorizing pets
structure Tag {
    @required
    key: String

    @required
    value: String
}

list PetList {
    member: Pet
}

list TagList {
    member: Tag
}

map AttributeMap {
    key: String
    value: String
}

/// Species of pet
enum PetSpecies {
    DOG
    CAT
    BIRD
    FISH
    REPTILE
    OTHER
}

/// Status of a pet
enum PetStatus {
    AVAILABLE
    PENDING
    SOLD
}

// Error types

/// Validation error for invalid input
@error("client")
@httpError(400)
structure ValidationError {
    @required
    message: String

    fieldErrors: FieldErrorList
}

structure FieldError {
    @required
    field: String

    @required
    message: String
}

list FieldErrorList {
    member: FieldError
}

/// Resource not found error
@error("client")
@httpError(404)
structure NotFoundError {
    @required
    message: String

    resourceType: String

    resourceId: String
}

/// Internal server error
@error("server")
@httpError(500)
structure InternalServerError {
    @required
    message: String

    requestId: String
}
